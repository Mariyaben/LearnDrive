"""Population-weighted, planar access analysis. Coordinates must be in metres.

The default nearest search is a small-data reference implementation. QGIS supplies
an R-tree lookup through ``nearest`` for production runs. No external packages.
"""

from dataclasses import dataclass
from math import hypot, isfinite
from typing import Callable, Iterable, Optional, Tuple


@dataclass(frozen=True)
class Demand:
    id: str
    name: str
    x: float
    y: float
    population: float


@dataclass(frozen=True)
class Facility:
    id: str
    name: str
    x: float
    y: float


@dataclass(frozen=True)
class Access:
    demand: Demand
    facility: Facility
    distance_m: float
    served: bool
    priority: float  # population * excess distance in km; not an optimal site score
    gap_rank: int  # 0 for served; 1 is the highest-priority gap


@dataclass(frozen=True)
class Analysis:
    rows: Tuple[Access, ...]
    facilities: Tuple[Facility, ...]
    threshold_m: float

    def summary(self):
        total = sum(row.demand.population for row in self.rows)
        served = sum(row.demand.population for row in self.rows if row.served)
        return {
            "communities": len(self.rows),
            "facilities": len(self.facilities),
            "population": total,
            "served_population": served,
            "underserved_population": total - served,
            "coverage_pct": 100 * served / total if total else None,
            "underserved_communities": sum(not row.served for row in self.rows),
            "weighted_mean_m": (
                sum(row.distance_m * row.demand.population for row in self.rows) / total
                if total else None
            ),
            "max_distance_m": max(row.distance_m for row in self.rows),
        }

    def scenarios(self, thresholds: Iterable[float]):
        total = sum(row.demand.population for row in self.rows)
        results = []
        for threshold in thresholds:
            _positive(threshold, "Scenario threshold")
            served = sum(
                row.demand.population for row in self.rows
                if row.distance_m <= threshold
            )
            results.append((threshold, served, 100 * served / total if total else None))
        return results


def _positive(value, name):
    if not isfinite(value) or value <= 0:
        raise ValueError(f"{name} must be a finite number greater than zero.")


def _validate_points(points, name):
    ids = set()
    for point in points:
        if not point.id or point.id in ids:
            raise ValueError(f"{name} IDs must be nonempty and unique: {point.id!r}")
        ids.add(point.id)
        if not isfinite(point.x) or not isfinite(point.y):
            raise ValueError(f"{name} {point.id}: coordinates must be finite.")


def analyze(
    demands: Iterable[Demand],
    facilities: Iterable[Facility],
    threshold_m: float,
    nearest: Optional[Callable[[float, float], str]] = None,
    check_cancel: Optional[Callable[[], None]] = None,
) -> Analysis:
    """Analyze every demand point; a point exactly on the threshold is served.

    Reject bad records rather than silently changing population denominators.
    ``nearest`` must return the ID of a nearest facility. Equidistant facilities
    have the same coverage/priority; the reference lookup breaks ties by ID.
    """
    _positive(threshold_m, "Access threshold")
    demands, facilities = tuple(demands), tuple(facilities)
    if not demands or not facilities:
        raise ValueError("At least one community and one facility are required.")
    _validate_points(demands, "Community")
    _validate_points(facilities, "Facility")
    for demand in demands:
        if not isfinite(demand.population) or demand.population < 0:
            raise ValueError(f"Community {demand.id}: population must be finite and nonnegative.")
    by_id = {facility.id: facility for facility in facilities}
    preliminary = []
    for demand in demands:
        if check_cancel:
            check_cancel()
        if nearest:
            facility_id = nearest(demand.x, demand.y)
            if facility_id not in by_id:
                raise ValueError("Nearest lookup returned an unknown facility ID.")
            facility = by_id[facility_id]
        else:
            facility = min(facilities, key=lambda f: (hypot(demand.x - f.x, demand.y - f.y), f.id))
        distance = hypot(demand.x - facility.x, demand.y - facility.y)
        priority = demand.population * max(0, distance - threshold_m) / 1000
        if not isfinite(distance) or not isfinite(priority):
            raise ValueError("Coordinate or population magnitude exceeds supported numeric range.")
        preliminary.append((demand, facility, distance, distance <= threshold_m, priority))
    gaps = sorted(
        (row for row in preliminary if not row[3]),
        key=lambda row: (-row[4], -row[2], row[0].id),
    )
    ranks = {row[0].id: index + 1 for index, row in enumerate(gaps)}
    rows = tuple(Access(*row, ranks.get(row[0].id, 0)) for row in preliminary)
    return Analysis(rows, facilities, threshold_m)
