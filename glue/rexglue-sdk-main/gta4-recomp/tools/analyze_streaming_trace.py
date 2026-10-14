#!/usr/bin/env python3
"""Analyze bounded streaming CSV events without equating state 1 with rendered pixels."""
from __future__ import annotations

import argparse
from collections import Counter
import csv
from dataclasses import dataclass, field
import io
import json
from pathlib import Path
import re
import sys
from typing import Iterable

FIELDS = ('time_ns', 'epoch', 'generation', 'entry', 'entity', 'model',
          'kind', 'before', 'after', 'bytes', 'value')
TRACE_EVENT_LIMIT = 65536
INVALID_ENTRY = 65535


@dataclass
class Resource:
    requests: list[tuple[int, int]] = field(default_factory=list)
    state_one: list[tuple[int, int]] = field(default_factory=list)


def percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    low = int(position)
    high = min(low + 1, len(ordered) - 1)
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def distribution(values: list[float]) -> dict:
    return {'count': len(values), 'median_ms': percentile(values, .5),
            'p95_ms': percentile(values, .95), 'p99_ms': percentile(values, .99),
            'maximum_ms': max(values) if values else None}


def analyze(rows: Iterable[dict[str, str]], runtime_log: str = '') -> dict:
    resources: dict[tuple[int, int, int], Resource] = {}
    kinds: Counter = Counter()
    transitions: Counter = Counter()
    classifications: Counter = Counter()
    entities: set[tuple[int, int, int]] = set()
    epochs: set[int] = set()
    invalid_entries = 0
    count = 0
    malformed = 0
    timestamp_regressions = 0
    previous_time = None
    first_time = None
    last_time = None
    for raw in rows:
        count += 1
        try:
            row = {key: int(raw[key]) for key in FIELDS}
            if any(value < 0 for value in row.values()):
                raise ValueError('negative unsigned field')
        except (KeyError, TypeError, ValueError):
            malformed += 1
            continue
        time = row['time_ns']
        if previous_time is not None and time < previous_time:
            timestamp_regressions += 1
        previous_time = time
        first_time = min(first_time, time) if first_time is not None else time
        last_time = max(last_time, time) if last_time is not None else time
        epochs.add(row['epoch'])
        kinds[row['kind']] += 1
        if row['kind'] == 3:
            classifications[row['after']] += 1
            entities.add((row['epoch'], row['entity'], row['model']))
        if row['kind'] == 2:
            transitions[f"{row['before']}->{row['after']}"] += 1
        if row['entry'] >= INVALID_ENTRY:
            invalid_entries += 1
            continue
        key = row['epoch'], row['generation'], row['entry']
        resource = resources.setdefault(key, Resource())
        if row['kind'] == 1:
            resource.requests.append((time, row['value']))
        elif row['kind'] == 2 and row['after'] == 1:
            resource.state_one.append((time, row['value']))

    latencies: list[float] = []
    late_by: list[float] = []
    first_state_count = 0
    deadline_known = 0
    request_timestamp_from_payload = 0
    invalid_latency = 0
    no_state_one_in_capture = 0
    for resource in resources.values():
        if not resource.state_one:
            no_state_one_in_capture += bool(resource.requests)
            continue
        # Re-entry into the same state must not count a resource twice.
        ready_time, embedded_request_time = min(resource.state_one)
        first_state_count += 1
        preceding = [(time, deadline) for time, deadline in resource.requests if time <= ready_time]
        request_time = min((time for time, _ in preceding), default=0)
        if embedded_request_time and (not request_time or embedded_request_time < request_time):
            # An earlier request event may have been dropped even when a later
            # duplicate request survived. Preserve the original timestamp.
            request_time = embedded_request_time
            request_timestamp_from_payload += 1
        if request_time:
            if ready_time < request_time:
                invalid_latency += 1
            else:
                latencies.append((ready_time - request_time) / 1_000_000)
        deadlines = [deadline for _, deadline in preceding if deadline]
        if deadlines:
            deadline_known += 1
            deadline = min(deadlines)
            if ready_time > deadline:
                late_by.append((ready_time - deadline) / 1_000_000)

    drops = [int(value) for value in re.findall(r'trace-dropped=(\d+)', runtime_log)]
    budgets = [dict(kind=kind, original_bytes=int(original), target_bytes=int(target),
                    effective_bytes=int(effective))
               for kind, original, target, effective in re.findall(
                   r'budget (virtual|physical) original=(\d+) target=(\d+)'
                   r'(?: allocator-clamped=\d+ reserve=\d+)? effective=(\d+)', runtime_log)]
    io_samples = [dict(accepted=int(a), completed=int(c), rejected=int(r), queued=int(q), active=int(n))
                  for a, c, r, q, n in re.findall(
                      r'host-file-io: accepted=(\d+) completed=(\d+) rejected=(\d+) queued=(\d+) active=(\d+)',
                      runtime_log)]
    return {
        'rows': count, 'malformed_rows': malformed, 'epochs': sorted(epochs),
        'capture_span_seconds': (last_time - first_time) / 1_000_000_000 if first_time is not None else None,
        'event_counts': dict(sorted(kinds.items())), 'state_transitions': dict(sorted(transitions.items())),
        'classification_codes': dict(sorted(classifications.items())),
        'observed_entity_model_handles': len(entities), 'unresolved_entry_events': invalid_entries,
        'resource_generations': len(resources), 'generations_reaching_state_one': first_state_count,
        'requested_generations_without_state_one_in_capture': no_state_one_in_capture,
        'request_to_first_state_one': distribution(latencies),
        'request_timestamp_recovered_from_state_payload': request_timestamp_from_payload,
        'generations_with_known_deadline': deadline_known,
        'generations_reaching_state_one_after_recorded_deadline': len(late_by),
        'state_one_deadline_lateness': distribution(late_by),
        'invalid_latency_samples': invalid_latency,
        'event_arrival_timestamp_regressions': timestamp_regressions,
        'recording_limit_reached': count >= TRACE_EVENT_LIMIT,
        'maximum_reported_dropped_events': max(drops) if drops else None,
        'budgets': budgets, 'last_host_io_sample': io_samples[-1] if io_samples else None,
        'interpretation': [
            'The resource key is (epoch, generation, entry); reused compact indices are not merged.',
            'A recorded state-1 transition is not evidence of GPU readiness or visible pixel coverage.',
            'A request lacking a later transition in this bounded capture is not automatically a failed request.',
            'Missing drop counters or reaching the row limit prevents a complete-capture claim.',
            'Deadline lateness uses the predictor recorded in the trace, not a measured visibility deadline.'
        ]
    }


def read_trace(path: Path, runtime_log: str) -> dict:
    with path.open(newline='') as stream:
        reader = csv.DictReader(stream)
        if tuple(reader.fieldnames or ()) != FIELDS:
            raise ValueError('Unexpected streaming trace schema')
        return analyze(reader, runtime_log)


def self_test() -> None:
    def row(time, epoch, generation, entry, kind, before=0, after=0, value=0):
        values = (time, epoch, generation, entry, 12, 42, kind, before, after, 0, value)
        return {key: str(value) for key, value in zip(FIELDS, values)}
    rows = [row(1000, 1, 1, 7, 1, value=1200),
            row(1500, 1, 1, 7, 2, 2, 1, 1000),
            row(1800, 1, 1, 7, 2, 3, 1, 1000),
            row(2000, 1, 2, 7, 1, value=3000),
            row(2200, 1, 2, 7, 2, 2, 1, 2000),
            row(4000, 2, 1, 7, 2, 2, 1, 3500),
            row(5000, 2, 1, 8, 1, value=7000),
            row(4500, 2, 1, INVALID_ENTRY, 3, 255, 2)]
    report = analyze(rows, 'trace-dropped=3 trace-dropped=7')
    assert report['resource_generations'] == 4
    assert report['generations_reaching_state_one'] == 3
    assert report['request_to_first_state_one']['count'] == 3
    assert report['generations_with_known_deadline'] == 2
    assert report['generations_reaching_state_one_after_recorded_deadline'] == 1
    assert report['requested_generations_without_state_one_in_capture'] == 1
    assert report['request_timestamp_recovered_from_state_payload'] == 1
    assert report['maximum_reported_dropped_events'] == 7
    assert report['event_arrival_timestamp_regressions'] == 1
    assert report['request_to_first_state_one']['maximum_ms'] == (1500 - 1000) / 1_000_000
    assert analyze([])['capture_span_seconds'] is None
    assert analyze([{}])['malformed_rows'] == 1
    assert analyze([row(10, 1, 1, 1, 2, 2, 1, 20)])['invalid_latency_samples'] == 1
    assert percentile([1, 3], .5) == 2
    partial = analyze([row(1200, 1, 1, 7, 1, value=1300),
                       row(1500, 1, 1, 7, 2, 2, 1, 1000)])
    assert partial['request_timestamp_recovered_from_state_payload'] == 1
    assert partial['request_to_first_state_one']['maximum_ms'] == (1500 - 1000) / 1_000_000
    print('PASS trace analyzer: generation reuse, duplicate transitions, bounds, drop counters and late events')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', nargs='?', type=Path)
    parser.add_argument('--runtime-log', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    if args.trace:
        log = args.runtime_log.read_text(errors='replace') if args.runtime_log else ''
        result = read_trace(args.trace, log)
        text = json.dumps(result, indent=2) + '\n'
        if args.output:
            args.output.write_text(text)
        print(text, end='')
        return int(result['malformed_rows'] != 0 or result['invalid_latency_samples'] != 0)
    if not args.self_test:
        parser.error('provide a trace or --self-test')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        raise SystemExit(1)
