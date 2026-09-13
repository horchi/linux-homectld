#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# garmin.py - Garmin Connect access for homectld (activities)
#
#  The daemon calls this script and reads JSON from stdout. Uses the
#  (unofficial) python library 'garminconnect', the login tokens are stored
#  in a directory (default /etc/homectld/garmin) and are valid for about a year.
#
#  garmin.py login [--email EMAIL]           interactive, asks password (and MFA code if enabled)
#  garmin.py status                          {"loggedin": true/false, ...}
#  garmin.py types                           the activity types of Garmin
#  garmin.py activities --all | --since DATE [--limit N]
#  garmin.py details ID                      summary of one activity (all values)
#  garmin.py track ID [--points N]           simplified GPS track (Garmin reduces it to N points)
#  garmin.py settype ID TYPEKEY              e.g. settype 1234567 windsurfing_v2
#  garmin.py settypes TYPEKEY < ids          bulk: one activity id per line on stdin, one login
#  garmin.py rename ID NAME
#  garmin.py renames < lines                  bulk: "ID<TAB>NAME" per line on stdin, one login
#  garmin.py delete ID                       deletes the activity at Garmin (!)
# -----------------------------------------------------------------------------

import argparse
import datetime
import getpass
import json
import os
import sys
import time

DEFAULT_TOKENS = "/etc/homectld/garmin"

def out(obj):
    print(json.dumps(obj, ensure_ascii=False))
    sys.stdout.flush()

def fail(msg, code=1):
    out({"error": msg})
    sys.exit(code)

def log(msg):
    print(msg, file=sys.stderr)
    sys.stderr.flush()

try:
    from garminconnect import Garmin
except ImportError:
    fail("python module 'garminconnect' not installed (see garmin/README.md)")

# -----------------------------------------------------------------------------
# login / session

def token_client(garmin):
    # the object holding load()/dump() of the tokens differs between versions
    return getattr(garmin, "client", None) or getattr(garmin, "garth")

def connect(tokens):
    if not os.path.isdir(tokens) or not os.listdir(tokens):
        fail("not logged in, call 'garmin.py login' first (tokens: %s)" % tokens)

    # verify_login=False: don't fetch the social profile on every call (fails now and then
    # with 'Failed to retrieve social profile', probably rate limiting); an expired token
    # shows up at the real request instead

    try:
        garmin = Garmin(verify_login=False)
    except TypeError:
        garmin = Garmin()

    error = None

    for attempt in range(3):
        try:
            garmin.login(tokens)
            return garmin
        except Exception as e:
            error = e
            log("login with stored tokens failed (%s), attempt %d/3" % (e, attempt + 1))
            time.sleep(1 + attempt)

    fail("login with stored tokens failed (%s), call 'garmin.py login' again" % error)

def do_login(args):
    email = args.email or input("Garmin email: ")
    password = getpass.getpass("Garmin password: ")

    def prompt_mfa():
        return input("MFA code (Garmin sent it by mail/app): ").strip()

    garmin = Garmin(email=email, password=password, prompt_mfa=prompt_mfa)

    try:
        garmin.login()
    except Exception as e:
        fail("login failed: %s" % e)

    os.makedirs(args.tokens, mode=0o700, exist_ok=True)
    token_client(garmin).dump(args.tokens)

    for f in os.listdir(args.tokens):
        os.chmod(os.path.join(args.tokens, f), 0o600)

    name = ""
    try:
        name = garmin.get_full_name()
    except Exception:
        pass

    out({"loggedin": True, "user": name, "tokens": args.tokens})

def do_status(args):
    if not os.path.isdir(args.tokens) or not os.listdir(args.tokens):
        out({"loggedin": False, "tokens": args.tokens})
        return

    try:
        garmin = Garmin()
        garmin.login(args.tokens)
        out({"loggedin": True, "user": garmin.get_full_name(), "tokens": args.tokens})
    except Exception as e:
        out({"loggedin": False, "tokens": args.tokens, "error": str(e)})

# -----------------------------------------------------------------------------
# activities

def to_epoch(gmt):
    # "2026-09-06 12:31:16" (GMT) -> epoch
    if not gmt:
        return 0
    return int(datetime.datetime.strptime(gmt[:19], "%Y-%m-%d %H:%M:%S").replace(tzinfo=datetime.timezone.utc).timestamp())

def num(v, default=0):
    try:
        return v if v is not None else default
    except Exception:
        return default

def normalize(a):
    t = a.get("activityType") or {}
    return {
        "id": a.get("activityId"),
        "name": a.get("activityName") or "",
        "type": t.get("typeKey") or "other",
        "typeid": t.get("typeId"),
        "parenttypeid": t.get("parentTypeId"),
        "start": to_epoch(a.get("startTimeGMT")),
        "startlocal": a.get("startTimeLocal") or "",
        "duration": int(num(a.get("duration"))),               # [s]
        "moving": int(num(a.get("movingDuration"))),           # [s]
        "elapsed": int(num(a.get("elapsedDuration"))),         # [s]
        "distance": float(num(a.get("distance"))),             # [m]
        "elevgain": float(num(a.get("elevationGain"))),        # [m]
        "elevloss": float(num(a.get("elevationLoss"))),        # [m]
        "avgspeed": float(num(a.get("averageSpeed"))),         # [m/s]
        "maxspeed": float(num(a.get("maxSpeed"))),             # [m/s]
        "avghr": int(num(a.get("averageHR"))),
        "maxhr": int(num(a.get("maxHR"))),
        "calories": int(num(a.get("calories"))),
        "steps": int(num(a.get("steps"))),
        "location": a.get("locationName") or "",
        "lat": float(num(a.get("startLatitude"))),
        "lon": float(num(a.get("startLongitude"))),
        "device": a.get("deviceId") or 0,
    }

def do_activities(args):
    garmin = connect(args.tokens)
    result = []

    if args.since:
        try:
            datetime.datetime.strptime(args.since, "%Y-%m-%d")
        except ValueError:
            fail("--since expects YYYY-MM-DD")

        today = datetime.date.today().isoformat()
        log("fetching activities since %s" % args.since)

        try:
            acts = garmin.get_activities_by_date(args.since, today)
        except Exception as e:
            fail("fetching activities failed: %s" % e)

        result = [normalize(a) for a in acts]
    else:
        # all activities, page by page (newest first)
        start = 0
        page = 50

        while True:
            log("fetching activities %d..%d" % (start, start + page))

            try:
                acts = garmin.get_activities(start, page)
            except Exception as e:
                fail("fetching activities failed: %s" % e)

            if not acts:
                break

            result += [normalize(a) for a in acts]
            start += len(acts)

            if len(acts) < page or (args.limit and start >= args.limit):
                break

    out({"since": args.since or "", "count": len(result), "activities": result})

def do_types(args):
    garmin = connect(args.tokens)

    try:
        types = garmin.get_activity_types()
    except Exception as e:
        fail("fetching activity types failed: %s" % e)

    out({"types": [{"key": t.get("typeKey"), "id": t.get("typeId"), "parent": t.get("parentTypeId")} for t in types]})

def do_details(args):
    garmin = connect(args.tokens)

    try:
        a = garmin.get_activity(args.id)
    except Exception as e:
        fail("fetching activity %s failed: %s" % (args.id, e))

    summary = a.get("summaryDTO") or {}
    t = a.get("activityTypeDTO") or {}

    out({
        "id": a.get("activityId"),
        "name": a.get("activityName") or "",
        "description": a.get("description") or "",
        "type": t.get("typeKey") or "",
        "location": a.get("locationName") or "",
        "summary": summary,                    # all values Garmin provides for this activity
    })

def do_track(args):
    garmin = connect(args.tokens)

    try:
        # maxchart 1 -> without the chart series, only the polyline is needed
        d = garmin.get_activity_details(args.id, maxchart=1, maxpoly=args.points)
    except Exception as e:
        fail("fetching track of activity %s failed: %s" % (args.id, e))

    poly = (d.get("geoPolylineDTO") or {})
    points = []

    for p in poly.get("polyline") or []:
        lat, lon = p.get("lat"), p.get("lon")
        if lat is None or lon is None:
            continue
        t = p.get("time")
        points.append([round(lat, 6), round(lon, 6),
                       int(t / 1000) if t else 0,                                    # epoch [s]
                       round(p["altitude"], 1) if p.get("altitude") is not None else None,
                       round(p["speed"], 2) if p.get("speed") is not None else None])  # [m/s]

    # Garmin often delivers 0.0 as speed for every polyline point - then drop it,
    # the WEBIF computes the speed from distance and time of consecutive points

    if not any(p[4] for p in points):
        for p in points:
            p[4] = None

    out({"id": args.id, "requested": args.points, "count": len(points),
         "bounds": [poly.get("minLat"), poly.get("minLon"), poly.get("maxLat"), poly.get("maxLon")],
         "points": points})

def do_settype(args):
    garmin = connect(args.tokens)

    try:
        types = garmin.get_activity_types()
    except Exception as e:
        fail("fetching activity types failed: %s" % e)

    t = next((t for t in types if t.get("typeKey") == args.typekey), None)

    if not t:
        fail("unknown activity type '%s'" % args.typekey)

    try:
        garmin.set_activity_type(str(args.id), t["typeId"], t["typeKey"], t["parentTypeId"])
        a = garmin.get_activity(args.id)
    except Exception as e:
        fail("changing the type of activity %s failed: %s" % (args.id, e))

    out({"id": args.id, "type": (a.get("activityTypeDTO") or {}).get("typeKey"), "typeid": t["typeId"], "parenttypeid": t["parentTypeId"]})

def do_settypes(args):
    # bulk change, ids from stdin (one per line); paced to be nice to the Garmin API
    garmin = connect(args.tokens)

    try:
        types = garmin.get_activity_types()
    except Exception as e:
        fail("fetching activity types failed: %s" % e)

    t = next((t for t in types if t.get("typeKey") == args.typekey), None)

    if not t:
        fail("unknown activity type '%s'" % args.typekey)

    ids = [int(l.strip()) for l in sys.stdin if l.strip()]
    ok, failed = [], []

    for n, aid in enumerate(ids, 1):
        try:
            garmin.set_activity_type(str(aid), t["typeId"], t["typeKey"], t["parentTypeId"])
            ok.append(aid)
            log("%d/%d %d ok" % (n, len(ids), aid))
        except Exception as e:
            failed.append({"id": aid, "error": str(e)})
            log("%d/%d %d FAILED: %s" % (n, len(ids), aid, e))

        time.sleep(args.pause)

    out({"type": t["typeKey"], "typeid": t["typeId"], "parenttypeid": t["parentTypeId"], "ok": ok, "failed": failed})

def do_renames(args):
    garmin = connect(args.tokens)
    ok, failed = [], []
    lines = [l.rstrip("\n") for l in sys.stdin if l.strip()]

    for n, line in enumerate(lines, 1):
        aid, name = line.split("\t", 1)

        try:
            garmin.set_activity_name(aid, name)
            ok.append({"id": int(aid), "name": name})
            log("%d/%d %s -> '%s' ok" % (n, len(lines), aid, name))
        except Exception as e:
            failed.append({"id": int(aid), "error": str(e)})
            log("%d/%d %s FAILED: %s" % (n, len(lines), aid, e))

        time.sleep(args.pause)

    out({"ok": ok, "failed": failed})

def do_delete(args):
    garmin = connect(args.tokens)

    try:
        garmin.delete_activity(str(args.id))
    except Exception as e:
        fail("deleting activity %s failed: %s" % (args.id, e))

    out({"id": args.id, "deleted": True})

def do_rename(args):
    garmin = connect(args.tokens)

    try:
        garmin.set_activity_name(str(args.id), args.name)
    except Exception as e:
        fail("renaming activity %s failed: %s" % (args.id, e))

    out({"id": args.id, "name": args.name})

# -----------------------------------------------------------------------------

def main():
    p = argparse.ArgumentParser(description="Garmin Connect access for homectld")
    p.add_argument("--tokens", default=os.environ.get("GARMIN_TOKENS", DEFAULT_TOKENS), help="token directory (default %s)" % DEFAULT_TOKENS)
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("login");  s.add_argument("--email");  s.set_defaults(fct=do_login)
    s = sub.add_parser("status"); s.set_defaults(fct=do_status)
    s = sub.add_parser("types");  s.set_defaults(fct=do_types)
    s = sub.add_parser("activities")
    g = s.add_mutually_exclusive_group(required=True)
    g.add_argument("--all", action="store_true")
    g.add_argument("--since", metavar="YYYY-MM-DD")
    s.add_argument("--limit", type=int, default=0, help="stop after N activities (--all)")
    s.set_defaults(fct=do_activities)
    s = sub.add_parser("details"); s.add_argument("id", type=int); s.set_defaults(fct=do_details)
    s = sub.add_parser("track");   s.add_argument("id", type=int); s.add_argument("--points", type=int, default=1000); s.set_defaults(fct=do_track)
    s = sub.add_parser("settype"); s.add_argument("id", type=int); s.add_argument("typekey"); s.set_defaults(fct=do_settype)
    s = sub.add_parser("settypes"); s.add_argument("typekey"); s.add_argument("--pause", type=float, default=0.5); s.set_defaults(fct=do_settypes)
    s = sub.add_parser("renames"); s.add_argument("--pause", type=float, default=0.5); s.set_defaults(fct=do_renames)
    s = sub.add_parser("delete");  s.add_argument("id", type=int); s.set_defaults(fct=do_delete)
    s = sub.add_parser("rename");  s.add_argument("id", type=int); s.add_argument("name"); s.set_defaults(fct=do_rename)

    args = p.parse_args()
    args.fct(args)

if __name__ == "__main__":
    main()
