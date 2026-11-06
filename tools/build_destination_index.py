"""Compile regional OSM destinations into an Android-compatible read-only FTS4 index."""
import argparse
import hashlib
import json
from pathlib import Path
import sqlite3
import unicodedata

def normalize(value):
    value=''.join(c for c in unicodedata.normalize('NFD',value.lower()) if not unicodedata.category(c).startswith('M'))
    return ' '.join(''.join(c if c.isalnum() else ' ' for c in value).split())

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a new index filename')
    rows=json.loads(a.source.read_text(encoding='utf-8'))
    with sqlite3.connect(a.output) as db:
        db.execute('PRAGMA application_id=0x4C444E56');db.execute('PRAGMA user_version=1')
        db.execute('CREATE TABLE destinations(name TEXT NOT NULL,lat REAL NOT NULL,lon REAL NOT NULL,kind TEXT NOT NULL,locality TEXT NOT NULL,name_key TEXT NOT NULL,kind_rank INTEGER NOT NULL,search TEXT NOT NULL)')
        db.executemany('INSERT INTO destinations VALUES(?,?,?,?,?,?,?,?)',[
            (r['name'],r['lat'],r['lon'],r.get('kind','place'),r.get('locality',''),normalize(r['name']),
             0 if r.get('kind') in ('city','town','village','suburb') else 1,
             normalize(' '.join([r['name']]+r.get('aliases',[])+[r.get('locality','')]))) for r in rows])
        db.execute("CREATE VIRTUAL TABLE lookup USING fts4(search,content='destinations',tokenize=unicode61,prefix='2,3,4')")
        db.execute("INSERT INTO lookup(lookup) VALUES('rebuild')")
        db.execute("INSERT INTO lookup(lookup) VALUES('optimize')")
        db.commit();db.execute('VACUUM')
        assert db.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
        for q in ['kochi*','lulu*','thrissur*','medical* college*']:
            count=db.execute('SELECT count(*) FROM lookup WHERE lookup MATCH ?',(q,)).fetchone()[0]
            assert count>0,q
            print(q,count,flush=True)
    with a.output.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
    report={'entries':len(rows),'bytes':a.output.stat().st_size,'sha256':digest,'schema':'LearnDrive destination FTS4 v1','source_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest()}
    a.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report))
if __name__=='__main__':main()
