#!/usr/bin/env python3
"""Join exported Metal encoder GPU timings to native GTA IV scope membership.

Input: xctrace XML tables or the equivalent decoded JSONL tables. This tool never
estimates individual draw/shader duration from CPU time or geometry counts.
"""
from __future__ import annotations
import argparse
import bisect
from collections import Counter, defaultdict
import csv
import html
import json
from pathlib import Path
import re
from typing import Any, Iterable
import xml.etree.ElementTree as ET

SEMANTIC = {0: "unknown", 1: "scene-to-gbuffer", 2: "lights-to-screen",
            3: "light-setup", 4: "light-draw", 5: "radar", 6: "composite-postfx"}
TRUSTED = {"executed-list-header", "executed-task-record", "header-and-task-record"}
FIELDS = re.compile(r"(?:^|\s)([a-z_]+)=([^\s]+)")

def read_rows(path: Path) -> Iterable[dict[str, Any]]:
    if path.suffix == ".jsonl":
        with path.open() as stream:
            for line in stream:
                yield json.loads(line)
        return
    refs: dict[str, Any] = {}
    columns: list[str] = []
    def decode(element: ET.Element) -> dict[str, Any]:
        if "ref" in element.attrib:
            return refs[element.attrib["ref"]]
        value = {"value": (element.text or "").strip(), "fmt": element.get("fmt", ""),
                 "children": {child.tag: decode(child) for child in element}}
        if "id" in element.attrib:
            refs[element.attrib["id"]] = value
        return value
    def scalar(value: dict[str, Any] | None) -> Any:
        if value is None:
            return None
        raw = value["value"]
        return int(raw) if re.fullmatch(r"-?\d+", raw) else raw or value["fmt"]
    for _, element in ET.iterparse(path, events=("end",)):
        if element.tag == "schema":
            columns = [col.findtext("mnemonic") for col in element.findall("col")]
        elif element.tag == "row":
            if len(element) != len(columns):
                raise ValueError(f"Unexpected row shape: {path}")
            decoded = dict(zip(columns, (decode(child) for child in element)))
            row = {key: scalar(value) for key, value in decoded.items()}
            thread = decoded.get("thread", {}).get("children", {})
            process = decoded.get("process") or thread.get("process")
            if process:
                row["process_id"] = scalar(process.get("children", {}).get("pid"))
            if "tid" in thread:
                row["thread_id"] = scalar(thread["tid"])
            yield row
            element.clear()

def load_table(directory: Path, name: str) -> list[dict[str, Any]]:
    for extension in (".jsonl", ".xml"):
        path = directory / (name + extension)
        if path.is_file():
            return list(read_rows(path))
    raise FileNotFoundError(f"Missing {name} in {directory}")

def belongs(row: dict[str, Any], pid: int) -> bool:
    if isinstance(row.get("process_id"), int):
        return row["process_id"] == pid
    value = str(row.get("process", ""))
    return value.endswith(f"({pid})") or f"pid: {pid})" in value

def union_ns(spans: Iterable[tuple[int, int]]) -> int:
    total = 0
    end: int | None = None
    for first, last in sorted(spans):
        if last <= first:
            continue
        if end is None or first > end:
            total += last - first
            end = last
        elif last > end:
            total += last - end
            end = last
    return total

def parse_fields(label: str) -> dict[str, str]:
    return dict(FIELDS.findall(label))

def validate_scope(scope: dict[str, Any]) -> str | None:
    summaries = scope["summaries"]
    members = scope["members"]
    if len(summaries) != 1:
        return "missing-or-duplicate-summary"
    total = summaries[0]
    try:
        if int(total["v"]) != 1:
            return "unsupported-version"
        if int(total["overflow"]) != 0:
            return "membership-overflow"
        if int(total["entries"]) != len(members):
            return "incomplete-membership"
        if any(int(member["v"]) != 1 for member in members):
            return "unsupported-member-version"
        for field in ("draws", "vertices", "indices"):
            if sum(int(member[field]) for member in members) != int(total[field]):
                return "membership-count-mismatch"
        for member in members:
            if int(member["draws"]) <= 0:
                return "invalid-member-count"
            int(member["semantic"])
            int(member["retail"].split(":", 1)[0])
            for stage in ("guest_vs", "guest_ps"):
                int(member[stage].split(":", 1)[0], 16)
        if int(total["clears"]) > 0:
            return "draws-and-explicit-clears"
    except (KeyError, ValueError, TypeError):
        return "malformed-membership"
    return None

def classify(scope: dict[str, Any], channel: str) -> tuple[str, str, str]:
    reason = scope["incomplete_reason"]
    members = scope["members"]
    if reason:
        return "unattributed: " + reason, "unattributed: " + reason, reason
    if not members:
        return "attachment-only", "no-issued-draw", "attachment-only"
    phases: set[str] = set()
    for member in members:
        semantic = int(member["semantic"])
        if semantic:
            phases.add("semantic/" + SEMANTIC.get(semantic, f"unknown-{semantic}"))
        elif member.get("source") in TRUSTED:
            phases.add("retail/" + member["retail"])
        else:
            phases.add("unattributed/" + member.get("source", "missing-source"))
    phase = next(iter(phases)) if len(phases) == 1 else "mixed-render-phases"
    field = "guest_vs" if channel == "Vertex" else "guest_ps" if channel == "Fragment" else None
    if field is None:
        return phase, "non-shader-channel", "scope-associated"
    selected_field = "selected_vs" if channel == "Vertex" else "selected_ps"
    shaders = {member.get(selected_field, member[field]) for member in members}
    shader = next(iter(shaders)) if len(shaders) == 1 else "mixed-guest-shaders"
    if len(shaders) == 1 and int(shader.split(":", 1)[0], 16) == 0:
        shader = "no-" + channel.lower() + "-shader (fixed-function/attachment work)"
    precision = "single-guest-shader-associated" if len(shaders) == 1 else "mixed-shaders-not-divided"
    return phase, shader, precision

def independent_phase(scope: dict[str, Any], retail: bool) -> str:
    """Keep original game provenance separate from the renderer's phase enum."""
    reason = scope["incomplete_reason"]
    if reason:
        return "unattributed: " + reason
    if not scope["members"]:
        return "attachment-only"
    values: set[str] = set()
    for member in scope["members"]:
        if retail:
            source = member.get("source", "missing-source")
            value = "retail/" + member["retail"] if source in TRUSTED else "unattributed/" + source
        else:
            phase = int(member["semantic"])
            value = "semantic/" + SEMANTIC.get(phase, f"unknown-{phase}")
        values.add(value)
    return next(iter(values)) if len(values) == 1 else "mixed-retail-phases" if retail else "mixed-semantic-phases"


def analyze(events: list[dict[str, Any]], encoders: list[dict[str, Any]],
            gpu: list[dict[str, Any]], pid: int,
            start_ns: int | None = None, end_ns: int | None = None) -> dict[str, Any]:
    events = [row for row in events if belongs(row, pid)]
    scopes: list[dict[str, Any]] = []
    index: dict[tuple[Any, int, int], list[dict[str, Any]]] = defaultdict(list)
    by_thread: dict[Any, list[dict[str, Any]]] = defaultdict(list)
    metadata = []
    for row in events:
        label = row.get("event-label", "")
        if label.startswith("GTA4/render/RecordNativeFrame scope="):
            fields = parse_fields(label)
            try:
                identity = (row.get("thread_id"), int(fields["frame"]), int(fields["scope"]))
            except (KeyError, ValueError):
                continue
            scope = {"id": len(scopes), "thread": identity[0], "frame": identity[1],
                     "scope": identity[2], "start": row["start"],
                     "end": row["start"] + row["duration"], "label": label,
                     "summaries": [], "members": []}
            scopes.append(scope)
            index[identity].append(scope)
            by_thread[identity[0]].append(scope)
        elif label.startswith(("GTA4/scope-summary ", "GTA4/scope-member ")):
            metadata.append(row)
    orphan = 0
    for row in metadata:
        fields = parse_fields(row["event-label"])
        try:
            if not isinstance(row.get("thread_id"), int):
                raise ValueError("Missing thread identity")
            identity = (row["thread_id"], int(fields["frame"]), int(fields["scope"]))
        except (KeyError, ValueError):
            orphan += 1
            continue
        # Summary records are siblings emitted immediately after the outer
        # scope closes, not CPU timing intervals enclosing the GPU work. Match
        # the explicit identity, never nearest-time or label substring guesses.
        candidates = [scope for scope in index.get(identity, [])
                      if scope["end"] <= row["start"]]
        if row["event-label"].startswith("GTA4/scope-summary "):
            candidates = [scope for scope in candidates
                          if parse_fields(scope["label"]).get("cmd") == fields.get("first_cmd")]
        if len(candidates) != 1:
            orphan += 1
            continue
        key = "summaries" if row["event-label"].startswith("GTA4/scope-summary ") else "members"
        candidates[0][key].append(fields)
    for scope in scopes:
        scope["incomplete_reason"] = validate_scope(scope)
    starts = {}
    prefix_ends = {}
    for thread, rows in by_thread.items():
        rows.sort(key=lambda row: row["start"])
        starts[thread] = [row["start"] for row in rows]
        maximum = 0
        prefix_ends[thread] = []
        for row in rows:
            maximum = max(maximum, row["end"])
            prefix_ends[thread].append(maximum)
    encmap = {}
    for row in encoders:
        if not belongs(row, pid):
            continue
        thread = row.get("thread_id")
        rows = by_thread.get(thread, []) if isinstance(thread, int) else []
        begin, end = row["start"], row["start"] + row["duration"]
        pos = bisect.bisect_right(starts.get(thread, []), begin) if rows else 0
        candidates = []
        # Natural scopes on a submission thread do not overlap. Inspect every
        # preceding overlapping scope so malformed/nested traces stay ambiguous.
        for offset in range(pos - 1, -1, -1):
            if prefix_ends[thread][offset] < begin:
                break
            scope = rows[offset]
            if scope["start"] <= begin and end <= scope["end"]:
                candidates.append(scope)
        encmap[row["encoder-id"]] = {"encoder": row,
            "scope": candidates[0] if len(candidates) == 1 else None,
            "reason": ("missing-thread-identity" if not isinstance(thread, int) else
                       "ambiguous-scope" if len(candidates) > 1 else "no-complete-scope")}
    selected = [row for row in gpu if belongs(row, pid) and isinstance(row.get("duration"), int)]
    if not selected:
        raise ValueError("No GPU records for selected process")
    lo = start_ns if start_ns is not None else min(row["start"] for row in selected)
    hi = end_ns if end_ns is not None else max(row["start"] + row["duration"] for row in selected)
    if hi <= lo:
        raise ValueError("Empty time interval")
    observations = []
    coverage = Counter()
    for row in selected:
        begin, end = max(lo, row["start"]), min(hi, row["start"] + row["duration"])
        if end <= begin:
            continue
        matched = encmap.get(row["encoder-id"])
        scope = matched["scope"] if matched else None
        if matched and matched["encoder"].get("cmdbuffer-id") != row.get("cmdbuffer-id"):
            scope = None
            reason = "command-buffer-identity-mismatch"
        else:
            reason = matched["reason"] if matched else "missing-encoder"
        if scope:
            phase, shader, precision = classify(scope, row["channel-name"])
            coverage["scope_joined_records"] += 1
        else:
            phase = "backend/" + (matched["encoder"].get("encoder-label", "unknown") if matched else "unknown")
            shader = "unattributed: " + reason
            precision = reason
        coverage[precision] += 1
        observations.append({"start": begin, "end": end, "duration_ns": end-begin,
                             "channel": row["channel-name"], "phase": phase,
                             "retail_phase": independent_phase(scope, True) if scope else "unattributed: " + reason,
                             "semantic_phase": independent_phase(scope, False) if scope else "unattributed: " + reason,
                             "guest_shader": shader, "precision": precision,
                             "encoder_id": row["encoder-id"], "command_buffer_id": row.get("cmdbuffer-id"),
                             "scope_id": scope["id"] if scope else None})
    def ranking(field: str) -> list[dict[str, Any]]:
        groups = defaultdict(list)
        for row in observations:
            groups[(row["channel"], row[field])].append(row)
        result = []
        for (channel, name), rows in groups.items():
            result.append({"channel": channel, "name": name, "gpu_sum_ms": sum(x["duration_ns"] for x in rows)/1e6,
                           "gpu_union_ms": union_ns((x["start"], x["end"]) for x in rows)/1e6,
                           "encoders": len({x["encoder_id"] for x in rows}), "records": len(rows)})
        return sorted(result, key=lambda row: row["gpu_union_ms"], reverse=True)
    return {"schema_version": 1, "pid": pid, "window_ns": [lo, hi], "window_seconds": (hi-lo)/1e9,
            "scope_count": len(scopes), "orphan_metadata": orphan,
            "scope_status": dict(Counter(scope["incomplete_reason"] or "complete" for scope in scopes)),
            "coverage": dict(coverage), "gpu_records": len(observations),
            "gpu_summed_interval_ns": sum(row["duration_ns"] for row in observations),
            "all_channel_gpu_union_ms": union_ns((row["start"],row["end"]) for row in observations)/1e6,
            "phase_ranking": ranking("phase"), "guest_shader_ranking": ranking("guest_shader"),
            "retail_phase_ranking": ranking("retail_phase"),
            "semantic_phase_ranking": ranking("semantic_phase"),
            "observations": observations, "scopes": scopes,
            "timing_definition": "GPU encoder activity associated with scope membership. Includes fixed-function/attachment work; not isolated shader instruction time. Mixed encoders are never divided by draw or geometry counts. GPU channels overlap."}

def write_result(result: dict[str, Any], output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    (output/"native-gpu-scope-analysis.json").write_text(json.dumps(result, indent=2))
    for key in ("phase_ranking", "retail_phase_ranking", "semantic_phase_ranking", "guest_shader_ranking"):
        with (output/(key.replace("_", "-")+".csv")).open("w",newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["channel","name","gpu_sum_ms","gpu_union_ms","encoders","records"])
            writer.writeheader();writer.writerows(result[key])
    tables=[]
    for title,key in (("Recorded GTA IV phases","retail_phase_ranking"),("Native-renderer semantic phases","semantic_phase_ranking"),("Guest shader associations","guest_shader_ranking")):
        body="".join(f'<tr><td>{html.escape(row["channel"])}</td><td>{html.escape(row["name"])}</td><td>{row["gpu_union_ms"]:.3f}</td><td>{row["encoders"]}</td></tr>' for row in result[key])
        tables.append(f'<h2>{title}</h2><table><thead><tr><th>GPU channel</th><th>Association</th><th>Active GPU ms in window</th><th>Encoders</th></tr></thead><tbody>{body}</tbody></table>')
    # Keep every scope available, but populate the page one frame at a time.
    # GPU durations are computed here, never inferred from the membership counts.
    by_scope = defaultdict(lambda: defaultdict(list))
    for item in result["observations"]:
        if item["scope_id"] is not None:
            by_scope[item["scope_id"]][item["channel"]].append((item["start"], item["end"]))
    scope_details = []
    for scope in result["scopes"]:
        channels = by_scope.get(scope["id"], {})
        gpu_times = {channel: f"{union_ns(spans)/1e6:.6f}" for channel, spans in channels.items()}
        scope_details.append({"frame":scope["frame"], "scope":scope["scope"],
                              "label":scope["label"], "status":scope["incomplete_reason"] or "complete",
                              "gpu_ms":gpu_times, "summary":scope["summaries"], "members":scope["members"]})
    payload = json.dumps(scope_details, separators=(",", ":")).replace("<", "\\u003c")
    detail = '<h2>Inspect every recorded scope</h2><p>Select a game frame to inspect its original phases, shader membership and measured GPU activity. Channels can overlap. A shader listed as a member does not receive a fabricated share of a mixed scope.</p><label>Game frame <select id="frames"></select></label> <label>Filter <input id="filter" placeholder="Phase, shader, operation, or status"></label><p id="scope-count"></p><div id="scopes"></div><script type="application/json" id="scope-data">'+payload+'</script>'
    detail += r"""<script>
const scopes=JSON.parse(document.getElementById('scope-data').textContent);
const frames=document.getElementById('frames'), filter=document.getElementById('filter');
for(const frame of new Set(scopes.map(s=>s.frame))){const o=document.createElement('option');o.value=String(frame);o.textContent=String(frame);frames.append(o);}
const firstTimed=scopes.find(s=>Object.keys(s.gpu_ms).length);if(firstTimed)frames.value=String(firstTimed.frame);
function renderScopes(){
 const target=document.getElementById('scopes');target.replaceChildren();
 const query=filter.value.toLowerCase();
 const selected=scopes.filter(s=>String(s.frame)===frames.value && (!query || JSON.stringify(s).toLowerCase().includes(query)));
 document.getElementById('scope-count').textContent=`${selected.length} matching scopes in selected frame; ${scopes.length} recorded scopes available.`;
 for(const scope of selected){
  const item=document.createElement('details'),title=document.createElement('summary');
  title.textContent=`Scope ${scope.scope}: ${scope.status} — ${scope.label}`;
  const timing=document.createElement('p');
  timing.textContent=Object.keys(scope.gpu_ms).length ? 'Measured GPU activity: '+Object.entries(scope.gpu_ms).map(([channel,ms])=>`${channel}: ${ms} ms`).join('; ') : 'No associated GPU interval in the selected time window.';
  const body=document.createElement('pre');body.textContent=JSON.stringify({summary:scope.summary,members:scope.members},null,2);
  item.append(title,timing,body);target.append(item);
 }
}
frames.addEventListener('change',renderScopes);filter.addEventListener('input',renderScopes);renderScopes();
</script>"""
    document='<!doctype html><meta charset="utf-8"><title>GTA IV GPU scope attribution</title><style>body{font:16px system-ui;max-width:1200px;margin:40px auto;padding:0 20px}table{border-collapse:collapse;width:100%}td,th{padding:10px;text-align:left;border-bottom:1px solid #bbb}pre{white-space:pre-wrap;overflow-wrap:anywhere}details{margin:8px 0}summary{cursor:pointer}input,select{font:inherit;padding:6px}td:nth-child(2){overflow-wrap:anywhere;max-width:650px}</style><h1>GTA IV GPU scope attribution</h1><p>'+html.escape(result["timing_definition"])+f'</p><p>Process {result["pid"]}; populated window {result["window_seconds"]:.3f} seconds.</p><pre>'+html.escape(json.dumps(result["coverage"],indent=2))+'</pre>'+''.join(tables)+detail
    (output/"native-gpu-scope-report.html").write_text(document)

def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exports", type=Path)
    parser.add_argument("--pid", required=True, type=int)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--start-ns", type=int);parser.add_argument("--end-ns", type=int)
    args=parser.parse_args()
    result=analyze(load_table(args.exports,"metal-application-event-interval"),
                   load_table(args.exports,"metal-application-encoders-list"),
                   load_table(args.exports,"metal-gpu-intervals"),args.pid,args.start_ns,args.end_ns)
    write_result(result,args.output)
    print(json.dumps({key:result[key] for key in ("pid","scope_count","orphan_metadata","scope_status","coverage","gpu_records")},indent=2))
if __name__=="__main__":
    main()
