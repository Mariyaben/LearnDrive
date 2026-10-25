"""Extract offline destinations, aliases and named landmarks from regional OSM."""
import argparse
import json
from pathlib import Path
import osmium


class Places(osmium.SimpleHandler):
    def __init__(self):
        super().__init__()
        self.rows = []
        self.seen = set()

    def details(self, tags):
        kind = tags.get('place') or tags.get('amenity') or tags.get('shop') or tags.get('tourism')
        if not kind and tags.get('railway') in ('station', 'halt'):
            kind = 'railway station'
        if not kind:
            return None
        names = []
        for key in ('name:en', 'name', 'int_name', 'name:ml', 'alt_name', 'official_name', 'short_name'):
            for name in (tags.get(key) or '').split(';'):
                name = name.strip()[:160]
                if name and name not in names:
                    names.append(name)
        if not names:
            return None
        locality = tags.get('addr:city') or tags.get('addr:suburb') or tags.get('addr:district') or ''
        return names[0], names[1:8], kind[:60], locality[:120]

    def add(self, details, lat, lon, identity, approximate=False):
        if details is None or not 8 <= lat <= 12.9 or not 74.8 <= lon <= 77.7:
            return
        name, aliases, kind, locality = details
        key = (name.casefold(), round(lat, 3), round(lon, 3), kind)
        if key in self.seen:
            return
        self.seen.add(key)
        self.rows.append(dict(name=name, aliases=aliases, kind=kind, locality=locality,
                              lat=lat, lon=lon, osm=identity, approximate=approximate))

    def node(self, node):
        details = self.details(node.tags)
        if details and node.location.valid():
            self.add(details, node.location.lat, node.location.lon, 'n'+str(node.id))

    def way(self, way):
        details = self.details(way.tags)
        if not details:
            return
        locations = [(n.lat, n.lon) for n in way.nodes if n.location.valid()]
        if not locations or len(locations) != len(way.nodes):
            return
        # Polygon representative, not an entrance or exact street address.
        self.add(details, sum(p[0] for p in locations)/len(locations),
                 sum(p[1] for p in locations)/len(locations), 'w'+str(way.id), True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path)
    p.add_argument("output", type=Path)
    a = p.parse_args()
    places = Places()
    places.apply_file(str(a.source), locations=True, idx='sparse_mem_array')
    places.rows.sort(key=lambda row: (row['name'].casefold(), row['osm']))
    from collections import Counter
    print('Destination categories:', Counter(row['kind'] for row in places.rows).most_common(12), flush=True)
    if len(places.rows) > 500000:
        raise ValueError('Region exceeds supported destination index; split the pack')
    data=json.dumps(places.rows, ensure_ascii=False, separators=(',', ':'))+'\n'
    if len(data.encode('utf-8')) > 96*1024*1024:
        raise ValueError('Destination index exceeds 96 MiB')
    a.output.write_text(data, encoding='utf-8')
    print(f'Wrote {len(places.rows)} offline destinations')


if __name__ == "__main__":
    main()
