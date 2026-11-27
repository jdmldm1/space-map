#!/usr/bin/env python3
"""Fetch spacecraft trajectories from NASA/JPL Horizons and write compact CSV files to assets/missions/.

    python3 scripts/gen_missions.py            # (re)generate everything
    python3 scripts/gen_missions.py voyager1   # just one mission

Each trajectory file has one line per sample:  unix_seconds,x,y,z,vx,vy,vz
  heliocentric missions: AU and AU/day, ICRF (equatorial J2000) frame, centre = Sun
  geocentric missions:   km and km/s,   ICRF frame, centre = Earth
The app interpolates with cubic Hermite splines, so positions between samples stay smooth.
events.csv lists planetary flybys detected from the data (closest approaches), so the
labels shown in the app always match the plotted path.

Horizons only publishes trajectories for missions that have an SPK kernel, so planned missions
appear only once JPL has a predicted trajectory for them.
"""
import math, os, re, sys, time, urllib.parse, urllib.request

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "missions")
API = "https://ssd.jpl.nasa.gov/api/horizons.api"

# key, Horizons id, centre, start, stop, samples
SUN, EARTH = "500@10", "500@399"
MISSIONS = [
    ("pioneer10", "-23", SUN, "1972-03-03", "2040-01-01", 700),
    ("pioneer11", "-24", SUN, "1973-04-06", "2040-01-01", 700),
    ("voyager1", "-31", SUN, "1977-09-06", "2040-01-01", 900),
    ("voyager2", "-32", SUN, "1977-08-21", "2040-01-01", 900),
    ("galileo", "-77", SUN, "1989-10-19", "2003-09-21", 700),
    ("cassini", "-82", SUN, "1997-10-16", "2017-09-15", 800),
    ("stardust", "-29", SUN, "1999-02-08", "2011-03-24", 500),
    ("hayabusa", "-130", SUN, "2003-05-10", "2010-06-13", 400),
    ("rosetta", "-226", SUN, "2004-03-03", "2016-09-30", 600),
    ("messenger", "-236", SUN, "2004-08-04", "2015-04-30", 700),
    ("dawn", "-203", SUN, "2007-09-28", "2018-11-01", 600),
    ("newhorizons", "-98", SUN, "2006-01-20", "2040-01-01", 700),
    ("msl", "-76", SUN, "2011-11-27", "2012-08-06", 200),
    ("mars2020", "-168", SUN, "2020-07-31", "2021-02-18", 200),
    ("dart", "-135", SUN, "2021-11-25", "2022-09-26", 200),
    ("hayabusa2", "-37", SUN, "2014-12-04", "2032-01-01", 700),
    ("osirisrex", "-64", SUN, "2016-09-09", "2032-01-01", 700),
    ("juno", "-61", SUN, "2011-08-06", "2030-01-01", 700),
    ("parker", "-96", SUN, "2018-08-12", "2030-01-01", 900),
    ("lucy", "-49", SUN, "2021-10-17", "2034-01-01", 700),
    ("psyche", "-255", SUN, "2023-10-14", "2032-01-01", 500),
    ("bepicolombo", "-121", SUN, "2018-10-21", "2027-12-01", 700),
    ("ulysses", "-55", SUN, "1990-10-07", "2009-06-30", 700),
    ("solarorbiter", "-144", SUN, "2020-02-11", "2031-01-01", 700),
    ("hera", "-91", SUN, "2024-10-08", "2027-02-01", 400),
    ("escapade", "-9", SUN, "2025-11-14", "2027-12-01", 500),
    ("mmx", "-239", SUN, "2026-10-20", "2031-12-31", 500),
    ("juice", "-28", SUN, "2023-04-15", "2035-01-01", 700),
    ("europaclipper", "-159", SUN, "2024-10-15", "2032-01-01", 600),
    # near-Earth, geocentric (kilometres)
    ("artemis1", "-1023", EARTH, "2022-11-16T07:00", "2022-12-11", 600),
    ("artemis2", "-1024", EARTH, "2026-04-01", "2026-04-10", 600),
    ("apollo11", "-399110", EARTH, "1969-07-16", "1969-07-30", 400),
    ("roman", "-211", EARTH, "2026-08-31", "2027-03-01", 300),
    ("jwst", "-170", EARTH, "2021-12-25T13:00", "2022-02-15", 400),
]
PLANET_IDS = {"Mercury": "199", "Venus": "299", "Earth": "399", "Mars": "499", "Jupiter": "599", "Saturn": "699", "Uranus": "799", "Neptune": "899"}
FLYBY_AU = {"Mercury": 0.02, "Venus": 0.03, "Earth": 0.03, "Mars": 0.03, "Jupiter": 0.5, "Saturn": 0.6, "Uranus": 0.6, "Neptune": 0.6}


def fetch(cmd, centre, start, stop, step, vec="2", units="AU-D"):
    p = {"format": "text", "COMMAND": f"'{cmd}'", "OBJ_DATA": "'NO'", "MAKE_EPHEM": "'YES'", "EPHEM_TYPE": "'VECTORS'",
         "CENTER": f"'{centre}'", "START_TIME": f"'{start}'", "STOP_TIME": f"'{stop}'", "STEP_SIZE": f"'{step}'",
         "VEC_TABLE": f"'{vec}'", "OUT_UNITS": f"'{units}'", "REF_PLANE": "'FRAME'", "CSV_FORMAT": "'YES'"}
    url = API + "?" + urllib.parse.urlencode(p)
    for attempt in range(4):
        try:
            return urllib.request.urlopen(url, timeout=120).read().decode()
        except Exception as exc:  # network hiccup
            err = exc
            time.sleep(2 + attempt * 3)
    raise RuntimeError(f"Horizons request failed: {err}")


def parse(text):
    if "$$SOE" not in text:
        return None
    rows = []
    for line in text.split("$$SOE")[1].split("$$EOE")[0].strip().splitlines():
        f = [c.strip() for c in line.split(",")]
        if len(f) < 5:
            continue
        jd = float(f[0])
        rows.append([(jd - 2440587.5) * 86400.0] + [float(v) for v in f[2:8] if v != ""])
    return rows


MONTHS = {m: i + 1 for i, m in enumerate("JAN FEB MAR APR MAY JUN JUL AUG SEP OCT NOV DEC".split())}


def coverage_limit(text):
    """'No ephemeris for target ... after A.D. 2017-SEP-15' -> ('after', 'YYYY-MM-DD HH:MM')"""
    m = re.search(r"(prior to|after) A\.D\. (\d{4})-([A-Z]{3})-(\d{2}) (\d{2}):(\d{2})", text)
    if not m:
        return None
    return m.group(1), f"{m.group(2)}-{MONTHS[m.group(3)]:02d}-{m.group(4)} {m.group(5)}:{m.group(6)}"


def trajectory(key, cmd, centre, start, stop, samples):
    units = "AU-D" if centre == SUN else "KM-S"
    for _ in range(4):
        span_days = max(1.0, (parse_date(stop) - parse_date(start)) / 86400.0)
        step_min = max(60, int(span_days * 1440.0 / samples))
        text = fetch(cmd, centre, start, stop, f"{step_min}m", units=units)
        rows = parse(text)
        if rows:
            return rows
        lim = coverage_limit(text)
        if not lim:
            raise RuntimeError(f"{key}: {text[:300]!r}")
        kind, when = lim
        # Horizons states the last/first epoch it can serve; pull in by 1 hour so the request lands inside it.
        if kind == "after":
            stop = shift(when, -3600)
        else:
            start = shift(when, 3600)
    raise RuntimeError(f"{key}: could not fit coverage")


def parse_date(s):
    s = s.replace("T", " ")
    fmt = "%Y-%m-%d %H:%M" if len(s) > 10 else "%Y-%m-%d"
    import calendar
    return float(calendar.timegm(time.strptime(s, fmt)))


def shift(s, seconds):
    return fmt_time(parse_date(s) + seconds, "%Y-%m-%d %H:%M")


def fmt_time(unix, fmt="%Y-%m-%d"):
    import datetime
    return (datetime.datetime(1970, 1, 1) + datetime.timedelta(seconds=unix)).strftime(fmt)


def planet_tracks(t_start, t_stop):
    tracks = {}
    start = fmt_time(t_start)
    stop = fmt_time(t_stop)
    for name, pid in PLANET_IDS.items():
        rows = parse(fetch(pid, SUN, start, stop, "1d", vec="1"))
        tracks[name] = rows
    return tracks


def detect_flybys(key, cmd, start, stop, tracks):
    """Daily-resolution closest approaches to each planet; a spacecraft that keeps returning to one planet
    (an orbiter) is collapsed to a single 'arrival' event."""
    import bisect
    fine = parse(fetch(cmd, SUN, start, stop, "1d", vec="1"))
    if not fine:
        return []
    events = []
    for name, trk in tracks.items():
        tt = [r[0] for r in trk]
        dist = []
        for r in fine:
            if r[0] < tt[0] or r[0] > tt[-1]:
                dist.append(1e9)
                continue
            i = min(max(bisect.bisect_left(tt, r[0]), 1), len(tt) - 1)
            a, b = trk[i - 1], trk[i]
            f = (r[0] - a[0]) / (b[0] - a[0]) if b[0] != a[0] else 0.0
            dist.append(math.dist(r[1:4], [a[k] + (b[k] - a[k]) * f for k in (1, 2, 3)]))
        mins = []
        for i in range(1, len(fine) - 1):
            if dist[i] < dist[i - 1] and dist[i] <= dist[i + 1] and dist[i] < FLYBY_AU[name]:
                if name == "Earth" and fine[i][0] - fine[0][0] < 86400.0 * 30:
                    continue  # still at the launch site
                mins.append((fine[i][0], dist[i]))
        groups = []
        for m in mins:
            if groups and m[0] - groups[-1][-1][0] < 150 * 86400.0:
                groups[-1].append(m)
            else:
                groups.append([m])
        for g in groups:
            if len(g) >= 4:
                events.append((key, g[0][0], f"{name} arrival (orbit)", min(d for _, d in g)))
            else:
                for t, d in g:
                    events.append((key, t, f"{name} flyby", d))
    return events


OUTER = [(4, "599"), (5, "699"), (6, "799"), (7, "899"), (8, "999")]  # index into the app's planet table, Horizons id


def write_planets():
    """High-precision outer-planet positions. The app's built-in planet model is accurate to a few arcminutes for
    these planets, which would leave a spacecraft visibly missing the planet it is flying past."""
    path = os.path.join(OUT, "planets.csv")
    with open(path, "w", newline="\n") as f:
        f.write("# idx,unix,x,y,z,vx,vy,vz  (AU, AU/day, heliocentric ICRF; idx = planet index in the app)\n")
        for idx, pid in OUTER:
            rows = parse(fetch(pid, SUN, "1970-01-01", "2040-12-31", "15d", vec="2"))
            for r in rows:
                f.write(",".join([str(idx), f"{r[0]:.0f}"] + [f"{v:.9g}" for v in r[1:7]]) + "\n")
            print(f"planet {pid}: {len(rows)} samples")


def main():
    if sys.argv[1:] == ["planets"]:
        os.makedirs(OUT, exist_ok=True)
        write_planets()
        return
    only = set(sys.argv[1:])
    os.makedirs(OUT, exist_ok=True)
    all_events = []
    rows_by_key = {}
    for key, cmd, centre, start, stop, samples in MISSIONS:
        if only and key not in only:
            continue
        try:
            rows = trajectory(key, cmd, centre, start, stop, samples)
        except Exception as exc:
            print(f"SKIP {key}: {exc}")
            continue
        with open(os.path.join(OUT, key + ".csv"), "w", newline="\n") as f:
            f.write("# unix,x,y,z,vx,vy,vz  (%s)\n" % ("AU, AU/day, heliocentric ICRF" if centre == SUN else "km, km/s, geocentric ICRF"))
            for r in rows:
                f.write(",".join([f"{r[0]:.0f}"] + [f"{v:.9g}" for v in r[1:7]]) + "\n")
        print(f"{key}: {len(rows)} samples {fmt_time(rows[0][0])} .. {fmt_time(rows[-1][0])}")
        if centre == SUN:
            rows_by_key[key] = (rows, cmd, start, stop)
    if rows_by_key:
        t_lo = min(v[0][0][0] for v in rows_by_key.values())
        t_hi = max(v[0][-1][0] for v in rows_by_key.values())
        tracks = planet_tracks(t_lo - 86400, t_hi + 86400)
        path = os.path.join(OUT, "events.csv")
        existing = []
        if only and os.path.exists(path):
            existing = [l for l in open(path).read().splitlines() if l and not l.startswith("#") and l.split(",")[0] not in only]
        with open(path, "w", newline="\n") as f:
            f.write("# key,unix,label,closest_approach_au\n")
            for l in existing:
                f.write(l + "\n")
            for key, (rows, cmd, _s, _e) in rows_by_key.items():
                a = fmt_time(rows[0][0] + 86400)
                b = fmt_time(rows[-1][0] - 86400)
                for ev in detect_flybys(key, cmd, a, b, tracks):
                    f.write(f"{ev[0]},{ev[1]:.0f},{ev[2]},{ev[3]:.5f}\n")
        print("events written")
    if not only:
        write_planets()


if __name__ == "__main__":
    main()
