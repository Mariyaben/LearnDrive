import json
from pathlib import Path
import random
import unittest

from accesslens.core import Demand, Facility, analyze


class CoreTests(unittest.TestCase):
    def setUp(self):
        self.facility = Facility("f", "Clinic", 0, 0)

    def test_euclidean_distance_and_boundary(self):
        result = analyze([Demand("d", "Village", 300, 400, 10)], [self.facility], 500)
        self.assertEqual(result.rows[0].distance_m, 500)
        self.assertTrue(result.rows[0].served)
        self.assertEqual(result.rows[0].gap_rank, 0)

    def test_coverage_is_population_weighted(self):
        result = analyze([Demand("a", "A", 0, 0, 900), Demand("b", "B", 2000, 0, 100)], [self.facility], 1000)
        summary = result.summary()
        self.assertEqual(summary["coverage_pct"], 90)
        self.assertEqual(summary["underserved_communities"], 1)
        self.assertEqual(summary["weighted_mean_m"], 200)
        self.assertEqual(summary["underserved_population"], 100)

    def test_priority_accounts_for_population_and_excess_distance(self):
        result = analyze([Demand("a", "A", 1100, 0, 1000), Demand("b", "B", 3000, 0, 100)], [self.facility], 1000)
        self.assertEqual([row.priority for row in result.rows], [100, 200])
        self.assertEqual([row.gap_rank for row in result.rows], [2, 1])

    def test_ties_are_deterministic(self):
        facilities = [Facility("z", "Z", -500, 0), Facility("a", "A", 500, 0)]
        self.assertEqual(analyze([Demand("d", "D", 0, 0, 1)], facilities, 100).rows[0].facility.id, "a")

    def test_zero_population_does_not_divide_by_zero(self):
        result = analyze([Demand("d", "D", 2000, 0, 0)], [self.facility], 100)
        self.assertIsNone(result.summary()["coverage_pct"])
        self.assertIsNone(result.summary()["weighted_mean_m"])
        self.assertEqual(result.scenarios([100])[0][2], None)

    def test_negative_nonfinite_populations_are_rejected(self):
        for value in (-1, float("nan"), float("inf")):
            with self.subTest(value=value), self.assertRaises(ValueError):
                analyze([Demand("d", "D", 0, 0, value)], [self.facility], 100)

    def test_nonfinite_coordinates_are_rejected(self):
        with self.assertRaises(ValueError):
            analyze([Demand("d", "D", float("nan"), 0, 1)], [self.facility], 100)

    def test_empty_inputs_are_rejected(self):
        for demand, facility in [([], [self.facility]), ([Demand("d", "D", 0, 0, 1)], [])]:
            with self.assertRaises(ValueError):
                analyze(demand, facility, 100)

    def test_bad_thresholds_are_rejected(self):
        for value in (0, -1, float("inf"), float("nan")):
            with self.subTest(value=value), self.assertRaises(ValueError):
                analyze([Demand("d", "D", 0, 0, 1)], [self.facility], value)

    def test_duplicate_ids_are_rejected(self):
        point = Demand("d", "D", 0, 0, 1)
        with self.assertRaises(ValueError):
            analyze([point, point], [self.facility], 100)
        with self.assertRaises(ValueError):
            analyze([point], [self.facility, self.facility], 100)

    def test_lookup_adapter_and_unknown_id(self):
        point = Demand("d", "D", 300, 400, 1)
        result = analyze([point], [self.facility], 100, nearest=lambda x, y: "f")
        self.assertEqual(result.rows[0].distance_m, 500)
        with self.assertRaises(ValueError):
            analyze([point], [self.facility], 100, nearest=lambda x, y: "missing")

    def test_cancellation_propagates(self):
        def cancel():
            raise InterruptedError("Canceled")
        with self.assertRaises(InterruptedError):
            analyze([Demand("d", "D", 0, 0, 1)], [self.facility], 100, check_cancel=cancel)

    def test_scenarios_are_monotone_and_use_inclusive_boundary(self):
        result = analyze([Demand("d", "D", 1000, 0, 10)], [self.facility], 1000)
        self.assertEqual([row[2] for row in result.scenarios([500, 1000, 1500])], [0, 100, 100])
        with self.assertRaises(ValueError):
            result.scenarios([0])

    def test_randomized_distance_and_population_invariants(self):
        rng = random.Random(26)
        facilities = [Facility(str(i), str(i), rng.uniform(-1000, 1000), rng.uniform(-1000, 1000)) for i in range(12)]
        demands = [Demand(str(i), str(i), rng.uniform(-2000, 2000), rng.uniform(-2000, 2000), rng.randint(0, 1000)) for i in range(150)]
        result = analyze(demands, facilities, 500)
        for row in result.rows:
            expected_squared = min((row.demand.x - f.x)**2 + (row.demand.y - f.y)**2 for f in facilities)
            self.assertAlmostEqual(row.distance_m**2, expected_squared, places=7)
        summary = result.summary()
        self.assertEqual(summary["population"], summary["served_population"] + summary["underserved_population"])
        ranks = sorted(row.gap_rank for row in result.rows if not row.served)
        self.assertEqual(ranks, list(range(1, len(ranks) + 1)))

    def test_committed_demo_matches_reference_summary(self):
        root = Path(__file__).resolve().parents[1]
        data = json.loads((root / "accesslens/data/projected_fixture.json").read_text(encoding="utf-8"))
        result = analyze([Demand(**row) for row in data["communities"]], [Facility(**row) for row in data["facilities"]], 1000)
        expected = json.loads((root / "demo/expected-summary.json").read_text(encoding="utf-8"))
        self.assertEqual(result.summary(), expected)
