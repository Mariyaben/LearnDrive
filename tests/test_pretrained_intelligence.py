"""Contract tests. These do not mock inference or count as model quality evidence."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from intelligence_model import bind_choice,choice_input,schema,validate,CHOICE_ALPHABET,LocalModel
from benchmark_intelligence import scenarios,blind_review
from replay_coaching import encode_world,decode_snapshot
from coaching_contract import empty_world
from generate_coaching_fixtures import attention


def context():
    return {'schema_version':2,'model_eligible':True,'vehicle':{'response':'RESPONSE_UNCERTAIN'},
        'navigation':{'provider_content_excluded':False,'window':0},'events':[],'history':[],
        'candidates':[{'index':i,'instruction_id':44+i,'event_id':100+i,'space':1,'urgent':False} for i in range(8)]}


class ProposalContract(unittest.TestCase):
    def test_all_eight_choices_have_distinct_non_reserved_bindings(self):
        c=context();compact,bindings=choice_input(c)
        self.assertEqual(len(set(CHOICE_ALPHABET)),8)
        for label,original in bindings.items():
            self.assertNotIn(label,'SD')
            proposal=validate(bind_choice(label,bindings),c)
            self.assertEqual(proposal['candidate_index'],original['index'])
        self.assertEqual(compact['choices']['D'],'DEFER')
        self.assertEqual(bind_choice('D',bindings)['decision'],'DEFER')
        self.assertEqual(bind_choice('S',bindings)['decision'],'SILENCE')

    def test_choice_budget_and_unknown_output_rejected(self):
        c=context();c['candidates'].append(c['candidates'][0])
        with self.assertRaisesRegex(ValueError,'budget'):choice_input(c)
        for text in ('','A because','Z','<think>S</think>'):
            with self.assertRaises(ValueError):bind_choice(text,choice_input(context())[1])

    def test_event_and_instruction_cannot_be_invented_or_rebound(self):
        c=context();value=bind_choice('A',choice_input(c)[1])
        for field,bad in [('event_id','1:999'),('instruction_id',5),('candidate_index',True)]:
            changed=copy.deepcopy(value);changed[field]=bad
            with self.assertRaises(ValueError):validate(changed,c)

    def test_quiet_cannot_contain_instruction(self):
        c=context();value=bind_choice('S',choice_input(c)[1]);value['instruction_id']=44
        with self.assertRaises(ValueError):validate(value,c)

    def test_model_cannot_create_or_downgrade_urgency(self):
        c=context();value=bind_choice('A',choice_input(c)[1]);value['decision']='WARN'
        with self.assertRaises(ValueError):validate(value,c)
        c['candidates'][0]['urgent']=True;value=bind_choice('A',choice_input(c)[1]);value['decision']='COACH'
        with self.assertRaises(ValueError):validate(value,c)

    def test_explanations_need_evidence(self):
        c=context()
        for reason in ('RESPONSE_OBSERVED','ALREADY_INFORMED','MANEUVER_WORKLOAD'):
            value=bind_choice('S',choice_input(c)[1]);value['reason_code']=reason
            with self.assertRaises(ValueError):validate(value,c)

    def test_no_calibrated_confidence_in_choice_binding(self):
        c=context();value=bind_choice('A',choice_input(c)[1]);self.assertIsNone(validate(value,c)['confidence'])
        for bad in (True,float('nan'),float('inf'),-1,1.1):
            value['confidence']=bad
            with self.assertRaises(ValueError):validate(value,c)

    def test_json_grammar_correlates_candidate_metadata(self):
        branches=schema(context())['oneOf'];self.assertEqual(len(branches),9)
        self.assertEqual(branches[1]['properties']['instruction_id'],{'const':44})
        self.assertEqual(branches[1]['properties']['decision'],{'const':'COACH'})

    def test_provider_and_ineligible_context_rejected_before_network(self):
        model=LocalModel('unused','unused','unused');c=context();c['navigation']['provider_content_excluded']=True
        with self.assertRaisesRegex(ValueError,'excluded'):model.infer(c)
        c['model_eligible']=False
        with self.assertRaisesRegex(ValueError,'eligible'):model.infer(c)

    def test_stale_scenario_never_begins_with_fresh_zero_clock(self):
        case=next(c for c in scenarios() if c['scenario_id']=='stale-camera')
        for row in case['rows']:
            capture=row['world']['capture_time']
            self.assertTrue(capture is None or row['published_at']-capture>=.59)

    def test_review_views_do_not_expose_engine_arm(self):
        runs=[{'scenario_id':'case','split':'development','events':{k:[{'time':1,'prompt':44,'voice_submitted':True}] for k in ('A_balanced','B_temporal','C_pretrained_hybrid')}}]
        with tempfile.TemporaryDirectory() as folder:
            blind_review(runs,Path(folder));panels=(Path(folder)/'review-panels.json').read_text()
            self.assertNotIn('A_balanced',panels);self.assertEqual(len(json.loads(panels)),3)

    def test_recorded_packed_contract_roundtrips_unknowns_and_original_capture(self):
        w=empty_world(2);attention(w,1.9);h,a,c=encode_world(w,2)
        clean=lambda values:[None if v in (float('inf'),-float('inf')) else v for v in values]
        packed={'packed_version':1,'header':clean(h),'actors':[clean(a)],'candidates':[clean(c)]}
        decoded=decode_snapshot(packed)
        self.assertIsNone(decoded['telemetry']['speed_kmh'])
        self.assertEqual(decoded['actors'][0]['capture_time'],1.9)
        self.assertEqual(decoded['candidates'][0]['expires_at'],2.25)
        self.assertEqual(encode_world(decoded,2)[0][:],h[:])


if __name__=='__main__':unittest.main()
