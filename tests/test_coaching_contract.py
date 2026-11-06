"""Synthetic software contracts; these cases are not instructor annotations."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import coaching_contract as contract
from generate_coaching_fixtures import actor, candidate, cases


class StructuredContractTest(unittest.TestCase):
    def test_generated_fixtures_are_explicitly_synthetic_and_valid(self):
        with tempfile.TemporaryDirectory() as folder:
            for fixture in cases():
                path = Path(folder) / (fixture["fixture_id"] + ".json")
                path.write_text(json.dumps(fixture), encoding="utf-8")
                value = contract.load_fixture(path)
                self.assertEqual(value["evidence_kind"], "synthetic")
                self.assertIsNone(value["instructor_labels"])
        self.assertEqual(len(cases()), 17)

    def test_duplicate_keys_and_nonfinite_json_fail(self):
        for text in ('{"a":1,"a":2}', '{"value":NaN}', '{"value":Infinity}'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                contract.strict_json(text)

    def test_unknown_measurements_cannot_be_qualified(self):
        world = contract.empty_world()
        world["telemetry"]["speed_valid"] = True
        with self.assertRaisesRegex(ValueError, "measurement"):
            contract.validate_world(world)
        world = contract.empty_world()
        world["telemetry"].update(pitch_valid=True, pitch_degrees=5)
        with self.assertRaisesRegex(ValueError, "calibration"):
            contract.validate_world(world)

    def test_bounds_and_duplicate_actor_identity(self):
        world = contract.empty_world()
        world["actors"] = [actor(0), actor(0)]
        with self.assertRaisesRegex(ValueError, "duplicate actor"):
            contract.validate_world(world)
        world["actors"] = []
        world["candidates"] = [candidate(0)] * 9
        with self.assertRaisesRegex(ValueError, "bounded list"):
            contract.validate_world(world)

    def test_original_capture_is_not_repaired_to_publication(self):
        fixture = next(value for value in cases() if value["fixture_id"] == "future-capture")
        self.assertEqual(fixture["observations"][0]["world"]["candidates"][0]["capture_time"], 1)
        contract.validate_world(fixture["observations"][0]["world"])

    def test_extra_world_fields_fail_instead_of_becoming_qualification(self):
        world = contract.empty_world()
        world["gps_is_good"] = True
        with self.assertRaisesRegex(ValueError, "exactly"):
            contract.validate_world(world)

    def test_shadow_rejects_google_export_and_unverified_recording(self):
        value=cases()[0]
        contract.validate_shadow_fixture(value)
        value["content_provenance"]["google_maps_content"]=True
        with self.assertRaisesRegex(ValueError,"Google exports"):
            contract.validate_shadow_fixture(value)
        value=cases()[0]
        value["evidence_kind"]="recorded_structured"
        value["content_provenance"].update(origin="consented_original",source_reference="unverified-original")
        with self.assertRaisesRegex(ValueError,"unverified recorded"):
            contract.validate_shadow_fixture(value)

    def test_publication_clock_must_increase(self):
        fixture = cases()[0]
        fixture["observations"][1]["published_at"] = 0
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "case.json"
            path.write_text(json.dumps(fixture), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "monotonic"):
                contract.load_fixture(path)

    def test_model_output_cannot_supply_arbitrary_instruction(self):
        for proposal in ({"schema_version":1,"mode":"COACH","candidate_index":0,"rationale":"x","text":"accelerate"},
                         {"schema_version":1,"mode":"COACH","candidate_index":8,"rationale":"x"},
                         {"schema_version":1,"mode":"SILENCE","candidate_index":0,"rationale":"x"}):
            with self.assertRaises(ValueError):
                contract.validate_proposal(proposal, 1)


class AnnotationContractTest(unittest.TestCase):
    def template(self):
        return json.loads((ROOT / "docs/coaching/instructor-annotation-template.json").read_text())

    def reviewed_fixture(self):
        # Synthetic schema-validation fixture only. No user-facing label file is
        # populated or claimed to be from a real reviewer.
        value = self.template()
        value.update(annotation_id="SYNTHETIC-validation-only", source_sha256="a" * 64,
                     split="development", reviewer={"role":"qualified_instructor","id":"SYNTHETIC-role"},
                     status="reviewed", reviewed_interval=[0,10], encounter_id="test-only", useful_window=[1,3],
                     decision="COACH", allowed_prompt_ids=[44], rationale="SYNTHETIC validation", uncertainty="synthetic")
        return value

    def test_blank_template_is_valid_but_unavailable_for_scoring(self):
        value = self.template()
        contract.validate_annotation(value)
        with self.assertRaisesRegex(ValueError, "unavailable"):
            contract.validate_annotation(value, require_reviewed=True)

    def test_engineer_cannot_be_relabelled_qualified_ground_truth(self):
        value = self.reviewed_fixture()
        value["reviewer"]["role"] = "engineer"
        with self.assertRaisesRegex(ValueError, "qualified"):
            contract.validate_annotation(value)

    def test_review_requires_timing_rationale_uncertainty_and_pinned_source(self):
        for field in ("source_sha256", "reviewed_interval", "rationale", "uncertainty", "useful_window"):
            value = self.reviewed_fixture()
            value[field] = None
            with self.subTest(field=field), self.assertRaises(ValueError):
                contract.validate_annotation(value)

    def test_reversed_window_fails(self):
        value = self.reviewed_fixture()
        value["useful_window"] = [3,1]
        with self.assertRaisesRegex(ValueError, "reversed"):
            contract.validate_annotation(value)


if __name__ == "__main__":
    unittest.main()
