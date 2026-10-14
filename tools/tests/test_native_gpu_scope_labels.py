import copy
import importlib.util
from pathlib import Path
import unittest
import tempfile
import json
import re

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("gpu_scope_analyzer", ROOT / "tools/analyze_native_gpu_scope_labels.py")
an = importlib.util.module_from_spec(spec)
spec.loader.exec_module(an)

class ScopeTests(unittest.TestCase):
    def fixture(self, different_phase=False, different_ps=False, different_vs=False):
        events=[]
        def event(start,duration,label,thread=2):
            return {"start":start,"duration":duration,"event-label":label,"process_id":1,"thread_id":thread}
        events.append(event(100,100,"GTA4/render/RecordNativeFrame scope=1 frame=2 cmd=7"))
        events.append(event(210,1,"GTA4/scope-summary v=1 frame=2 scope=1 first_cmd=7 entries=2 draws=2 vertices=6 indices=0 clears=0 overflow=0"))
        for i in range(2):
            semantic=2 if i and different_phase else 1
            vs=2 if i and different_vs else 1
            ps=3 if i and different_ps else 2
            events.append(event(220+i*2,1,f"GTA4/scope-member v=1 frame=2 scope=1 retail=31:scene-to-gbuffer source=executed-list-header semantic={semantic} guest_vs={vs:016X}:vs{vs} guest_ps={ps:016X}:ps{ps} draws=1 vertices=3 indices=0 pipeline=A mixed_pipelines=0 first_list=1 last_list=1"))
        enc=[{"start":110,"duration":50,"encoder-id":3,"cmdbuffer-id":4,"encoder-label":"test","process_id":1,"thread_id":2}]
        gpu=[{"start":300,"duration":50,"encoder-id":3,"cmdbuffer-id":4,"channel-name":"Vertex","process_id":1},
             {"start":320,"duration":100,"encoder-id":3,"cmdbuffer-id":4,"channel-name":"Fragment","process_id":1}]
        return events,enc,gpu
    def run_case(self,*args):return an.analyze(*args,pid=1)
    def test_selected_shader_source_is_distinct_from_guest_provenance(self):
        ev, enc, gpu = self.fixture()
        for row in ev[2:]:
            row['event-label'] += ' selected_ps=0000000000000002:motion_blur/gta_composite_mb_e2.glsl variant=2 samples=1'
        result = self.run_case(ev, enc, gpu)
        fragment = [x for x in result['guest_shader_ranking'] if x['channel'] == 'Fragment']
        self.assertEqual(fragment[0]['name'], '0000000000000002:motion_blur/gta_composite_mb_e2.glsl')

    def test_stock_and_replacement_in_one_scope_are_not_homogeneous(self):
        ev, enc, gpu = self.fixture()
        ev[2]['event-label'] += ' selected_ps=0000000000000002:stock variant=0 samples=1'
        ev[3]['event-label'] += ' selected_ps=0000000000000002:replacement variant=2 samples=1'
        result = self.run_case(ev, enc, gpu)
        fragment = [x for x in result['guest_shader_ranking'] if x['channel'] == 'Fragment']
        self.assertEqual(fragment[0]['name'], 'mixed-guest-shaders')

    def test_siblings_join_by_explicit_identity(self):
        r=self.run_case(*self.fixture())
        self.assertEqual(r['orphan_metadata'],0)
        self.assertEqual(r['scope_status'],{'complete':1})
        self.assertEqual(r['gpu_summed_interval_ns'],150)
        self.assertEqual(r['all_channel_gpu_union_ms'],0.00012)
    def test_mixed_fragment_cost_is_not_divided(self):
        r=self.run_case(*self.fixture(different_ps=True))
        fragment=[x for x in r['guest_shader_ranking'] if x['channel']=='Fragment']
        self.assertEqual(len(fragment),1)
        self.assertEqual(fragment[0]['name'],'mixed-guest-shaders')
        self.assertEqual(fragment[0]['gpu_sum_ms'],0.0001)
        self.assertEqual(r['observations'][0]['guest_shader'],'0000000000000001:vs1')
    def test_mixed_phases_remain_one_mixed_cost(self):
        r=self.run_case(*self.fixture(different_phase=True))
        self.assertEqual({v['phase'] for v in r['observations']},{'mixed-render-phases'})
        self.assertEqual(sum(v['duration_ns'] for v in r['observations']),150)
    def test_missing_member_is_not_a_homogeneous_scope(self):
        ev,enc,gpu=self.fixture();ev.pop()
        r=self.run_case(ev,enc,gpu)
        self.assertEqual(r['scope_status'],{'incomplete-membership':1})
    def test_overflow_is_not_treated_as_complete(self):
        ev,enc,gpu=self.fixture();ev[1]['event-label']=ev[1]['event-label'].replace('overflow=0','overflow=1')
        r=self.run_case(ev,enc,gpu)
        self.assertEqual(r['scope_status'],{'membership-overflow':1})
    def test_wrong_thread_cannot_join_metadata(self):
        ev,enc,gpu=self.fixture();ev[1]['thread_id']=10
        r=self.run_case(ev,enc,gpu)
        self.assertEqual(r['orphan_metadata'],1)
        self.assertEqual(r['scope_status'],{'missing-or-duplicate-summary':1})
    def test_command_buffer_identity_is_checked(self):
        ev,enc,gpu=self.fixture();gpu[0]['cmdbuffer-id']=99
        r=self.run_case(ev,enc,gpu)
        self.assertEqual(r['observations'][0]['precision'],'command-buffer-identity-mismatch')
    def test_partial_encoder_is_not_joined_by_nearest_label(self):
        ev,enc,gpu=self.fixture();enc[0]['duration']=150
        r=self.run_case(ev,enc,gpu)
        self.assertEqual({v['precision'] for v in r['observations']},{'no-complete-scope'})
    def test_geometry_counts_do_not_control_gpu_cost(self):
        ev,enc,gpu=self.fixture(different_ps=True)
        before=self.run_case(ev,enc,gpu)
        ev[1]['event-label']=ev[1]['event-label'].replace('draws=2 vertices=6','draws=100001 vertices=300003')
        ev[2]['event-label']=ev[2]['event-label'].replace('draws=1 vertices=3','draws=100000 vertices=300000')
        after=self.run_case(ev,enc,gpu)
        self.assertEqual(before['guest_shader_ranking'],after['guest_shader_ranking'])
    def test_clear_work_is_not_called_shader_execution(self):
        ev,enc,gpu=self.fixture();ev[1]['event-label']=ev[1]['event-label'].replace('clears=0','clears=1')
        r=self.run_case(ev,enc,gpu)
        self.assertEqual(r['scope_status'],{'draws-and-explicit-clears':1})
    def test_multiple_matching_outer_scopes_are_ambiguous(self):
        ev,enc,gpu=self.fixture();ev.append(copy.deepcopy(ev[0]))
        r=self.run_case(ev,enc,gpu)
        self.assertEqual(r['orphan_metadata'],3)
        self.assertEqual({v['precision'] for v in r['observations']},{'ambiguous-scope'})
    def test_window_clips_only_gpu_activity(self):
        ev,enc,gpu=self.fixture()
        r=an.analyze(ev,enc,gpu,1,325,340)
        self.assertEqual(r['gpu_summed_interval_ns'],30)
        self.assertEqual(r['all_channel_gpu_union_ms'],0.000015)
    def test_same_shader_on_other_process_does_not_join(self):
        ev,enc,gpu=self.fixture();enc[0]['process_id']=8
        r=self.run_case(ev,enc,gpu)
        self.assertEqual({v['precision'] for v in r['observations']},{'missing-encoder'})
    def test_first_command_mismatch_rejects_summary(self):
        ev,enc,gpu=self.fixture();ev[1]['event-label']=ev[1]['event-label'].replace('first_cmd=7','first_cmd=8')
        r=self.run_case(ev,enc,gpu)
        self.assertEqual(r['orphan_metadata'],1)
    def test_missing_thread_identity_cannot_join(self):
        ev,enc,gpu=self.fixture()
        for row in ev+enc:row.pop('thread_id',None)
        r=self.run_case(ev,enc,gpu)
        self.assertEqual(r['orphan_metadata'],3)
        self.assertEqual({v['precision'] for v in r['observations']},{'missing-thread-identity'})
    def test_summary_before_encoder_closes_is_rejected(self):
        ev,enc,gpu=self.fixture();ev[1]['start']=150
        r=self.run_case(ev,enc,gpu)
        self.assertEqual(r['orphan_metadata'],1)
        self.assertEqual(r['scope_status'],{'missing-or-duplicate-summary':1})
    def test_actual_retail_phase_is_retained_with_a_semantic_phase(self):
        ev,enc,gpu=self.fixture()
        for row in ev:row['event-label']=row['event-label'].replace('retail=31:scene-to-gbuffer','retail=17:water-reflection')
        r=self.run_case(ev,enc,gpu)
        self.assertEqual({x['retail_phase'] for x in r['observations']},{'retail/17:water-reflection'})
        self.assertEqual({x['semantic_phase'] for x in r['observations']},{'semantic/scene-to-gbuffer'})
    def test_same_semantic_cannot_hide_mixed_original_phases(self):
        ev,enc,gpu=self.fixture();ev[-1]['event-label']=ev[-1]['event-label'].replace('retail=31:scene-to-gbuffer','retail=17:water-reflection')
        r=self.run_case(ev,enc,gpu)
        self.assertEqual({x['retail_phase'] for x in r['observations']},{'mixed-retail-phases'})
        self.assertEqual({x['semantic_phase'] for x in r['observations']},{'semantic/scene-to-gbuffer'})
    def test_conflicting_original_provenance_is_not_trusted(self):
        ev,enc,gpu=self.fixture()
        for row in ev:row['event-label']=row['event-label'].replace('source=executed-list-header','source=conflicting-list-record')
        r=self.run_case(ev,enc,gpu)
        self.assertEqual({x['retail_phase'] for x in r['observations']},{'unattributed/conflicting-list-record'})
    def test_separate_phase_views_preserve_total_gpu_observations(self):
        r=self.run_case(*self.fixture(different_phase=True,different_ps=True))
        for key in ['phase_ranking','retail_phase_ranking','semantic_phase_ranking','guest_shader_ranking']:
            self.assertAlmostEqual(sum(x['gpu_sum_ms'] for x in r[key]),r['gpu_summed_interval_ns']/1e6)
    def test_report_retains_all_scopes_without_invented_shader_costs(self):
        r=self.run_case(*self.fixture(different_ps=True))
        # Rendering uses measured encoder intervals; member counts stay metadata.
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);an.write_result(r,root)
            document=(root/'native-gpu-scope-report.html').read_text()
            raw=re.search(r'<script type="application/json" id="scope-data">(.*?)</script>',document).group(1)
            payload=json.loads(raw)
            self.assertEqual(len(payload),r['scope_count'])
            self.assertEqual(payload[0]['gpu_ms']['Fragment'],'0.000100')
            self.assertEqual(len(payload[0]['members']),2)
            self.assertTrue((root/'retail-phase-ranking.csv').is_file())
            self.assertTrue((root/'semantic-phase-ranking.csv').is_file())
            self.assertNotIn('first 250 scopes',document)
if __name__=='__main__':unittest.main()
