"""Exercise prepared Kerala graph locally; not route legality/learner validation."""
import argparse
import json
from pathlib import Path
import statistics
import time


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--graph",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    import valhalla
    a.output.mkdir(parents=True,exist_ok=True)
    config=json.loads(Path("android/app/src/main/assets/navigation-valhalla-config.json").read_text())
    config["mjolnir"]["tile_extract"]=str(a.graph.resolve())
    config["mjolnir"]["tile_dir"]=""
    config["loki"]["actions"]=["route","trace_attributes","locate"]
    start=time.perf_counter();actor=valhalla.Actor(config);initialization=(time.perf_counter()-start)*1000
    cases=[
        ("kochi-alappuzha",(9.9679032,76.2444378),(9.4923271,76.3291487)),
        ("alappuzha-thiruvananthapuram",(9.4923271,76.3291487),(8.4882267,76.947551)),
        ("kozhikode-kannur",(11.2450558,75.7754716),(11.8763836,75.3737973)),
        ("palakkad-kochi",(10.7681928,76.6521319),(9.9679032,76.2444378)),
        ("kochi-local",(9.9679032,76.2444378),(9.9816,76.2999)),
    ]
    results=[]
    for name,origin,target in cases:
        request={"locations":[{"lat":origin[0],"lon":origin[1]},{"lat":target[0],"lon":target[1]}],
                 "costing":"auto","units":"kilometers","shape_format":"polyline6",
                 "directions_options":{"units":"kilometers","language":"en-US"}}
        encoded=json.dumps(request)
        first=time.perf_counter();raw=actor.route(encoded);first_ms=(time.perf_counter()-first)*1000
        response=json.loads(raw);trip=response["trip"]
        if trip["status"]!=0 or trip["units"]!="kilometers" or not trip["legs"][0]["shape"]:
            raise RuntimeError(name+": invalid route response")
        (a.output/(name+".json")).write_text(json.dumps(response,indent=2)+"\n")
        timings=[]
        for _ in range(20):
            t=time.perf_counter();actor.route(encoded);timings.append((time.perf_counter()-t)*1000)
        # Look up exact graph-side attributes on the returned geometry. This is
        # a consistency check on built data, not ground truth for a physical road.
        attributes=actor.trace_attributes({"encoded_polyline":trip["legs"][0]["shape"],"shape_match":"edge_walk","costing":"auto"})
        (a.output/(name+"-attributes.json")).write_text(json.dumps(attributes,indent=2)+"\n")
        edges=attributes.get("edges",[])
        sides=[edge["drive_on_right"] for edge in edges if "drive_on_right" in edge]
        admins=attributes.get("admins",[])
        if not sides or any(sides):raise RuntimeError(name+": left-driving graph evidence missing/incorrect")
        if not any(admin.get("country_code")=="IN" for admin in admins):raise RuntimeError(name+": India country evidence missing")
        results.append({"case":name,"length_km":trip["summary"]["length"],"first_route_ms":first_ms,
                        "warm_p50_ms":statistics.median(timings),"warm_max_ms":max(timings),
                        "maneuvers":len(trip["legs"][0]["maneuvers"]),"checked_left_driving_edges":len(sides),"country_admins":admins})
        print(json.dumps(results[-1]),flush=True)
    try:
        actor.route({"locations":[{"lat":51.5,"lon":-.1},{"lat":51.51,"lon":-.12}],"costing":"auto"})
    except Exception as error:
        outside={"rejected":True,"error":str(error)}
    else:raise RuntimeError("Uninstalled coverage unexpectedly routed")
    report={"engine":valhalla.__version__,"runtime":"WSL/Linux desktop native; not physical Android",
            "network_routing":False,"initialization_ms":initialization,"cases":results,"outside_coverage":outside,
            "meaning":"Functional data/routing consistency, not independently checked road legality, useful coaching, or ARM performance"}
    (a.output/"verification.json").write_text(json.dumps(report,indent=2)+"\n")


if __name__=="__main__":main()
