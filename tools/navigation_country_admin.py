"""Supply a clipped country parent when a state extract lacks its whole boundary.

The input must contain exactly one complete state polygon. The country polygon
is clipped to that same supported state extent; it does not assert a new border.
Uses the same bundled SQLite/SpatiaLite libraries as the pinned graph builder.
"""
import ctypes
import json
from pathlib import Path
import sqlite3


def kerala_parent(database, wheel):
    with sqlite3.connect(database) as con:
        rows = con.execute("SELECT rowid, admin_level, iso_code, name, drive_on_right FROM admins").fetchall()
    if len(rows) != 1 or rows[0][1:4] != (4, "KL", "Kerala"):
        raise ValueError("Expected exactly the complete Kerala admin polygon")
    libs = Path(wheel)/"pyvalhalla.libs"
    sql = ctypes.CDLL(str(next(libs.glob("libsqlite3-*.so*"))))
    spatial = ctypes.CDLL(str(next(libs.glob("libspatialite-*.so*"))))
    sql.sqlite3_open.argtypes=[ctypes.c_char_p,ctypes.POINTER(ctypes.c_void_p)]
    sql.sqlite3_close.argtypes=[ctypes.c_void_p]
    sql.sqlite3_exec.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.POINTER(ctypes.c_char_p)]
    spatial.spatialite_alloc_connection.restype=ctypes.c_void_p
    spatial.spatialite_init_ex.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int]
    spatial.spatialite_cleanup_ex.argtypes=[ctypes.c_void_p]
    db=ctypes.c_void_p()
    if sql.sqlite3_open(str(database).encode(),ctypes.byref(db)):
        raise RuntimeError("Cannot open country metadata database")
    cache=spatial.spatialite_alloc_connection()
    try:
        spatial.spatialite_init_ex(db,cache,0)
        statement=b"""BEGIN IMMEDIATE;
          INSERT INTO admins(admin_level,iso_code,parent_admin,name,name_en,drive_on_right,allow_intersection_names,default_language,supported_languages,geom)
          SELECT 2,'IN',NULL,'India','India',0,allow_intersection_names,default_language,supported_languages,geom FROM admins WHERE admin_level=4 AND iso_code='KL';
          UPDATE admins SET drive_on_right=0,parent_admin=(SELECT rowid FROM admins WHERE iso_code='IN' AND admin_level=2) WHERE iso_code='KL' AND admin_level=4;
          COMMIT;"""
        error=ctypes.c_char_p()
        if sql.sqlite3_exec(db,statement,None,None,ctypes.byref(error)):
            raise RuntimeError(error.value.decode() if error.value else "Country metadata update failed")
    finally:
        sql.sqlite3_close(db);spatial.spatialite_cleanup_ex(cache)
    with sqlite3.connect(database) as con:
        after=con.execute("SELECT rowid,admin_level,iso_code,name,drive_on_right FROM admins ORDER BY admin_level").fetchall()
    report={"before":rows,"after":after,"country_geometry":"India clipped to the complete Kerala polygon; no additional routing coverage"}
    Path(database).with_name("country-admin-review.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report),flush=True)


if __name__=="__main__":
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("database",type=Path);p.add_argument("wheel",type=Path)
    a=p.parse_args();kerala_parent(a.database,a.wheel)
