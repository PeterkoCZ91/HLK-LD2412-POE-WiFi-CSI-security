#!/usr/bin/env python3
"""Find retained MQTT topics that no live device will ever refresh again.

A retained tree whose device is merely switched off is NOT an orphan: this
firmware republishes all of its Home Assistant discovery entities on every
MQTT connect, so an unplugged node restores itself the moment it boots. The
only safe evidence that an identity is dead is that nothing published under it
for a long time while its peers did.

So this watches live traffic with retained messages suppressed, remembers when
each device identity last said anything, and at report time joins that against
the retained snapshot to show what each identity is still holding in Home
Assistant. It never publishes and never deletes -- the delete list is a human
decision, and the report is the input to it.

  watch:   tools/mqtt_orphan_watch.py --host <broker> --user U --pass P
  report:  tools/mqtt_orphan_watch.py --host <broker> --user U --pass P --report
"""
import argparse, json, os, subprocess, sys, time

DEFAULT_STATE = os.path.expanduser("~/poe2412-soak/mqtt_orphan_watch.json")
# Trees where a device identity is the second path segment.
IDENTITY_TREES = ("security", "esphome")


def identity_of(topic):
    parts = topic.split("/")
    if len(parts) >= 3 and parts[0] in IDENTITY_TREES:
        return f"{parts[0]}/{parts[1]}"
    return None


def load(path):
    try:
        with open(path) as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return {"started": None, "last_seen": {}, "counts": {}}


def save(path, state):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as fh:
        json.dump(state, fh, indent=1, sort_keys=True)
    os.replace(tmp, path)          # never leave a half-written state file


def sub_cmd(args, extra):
    cmd = ["mosquitto_sub", "-h", args.host, "-p", str(args.port)]
    if args.user:
        cmd += ["-u", args.user]
    if args.password:
        cmd += ["-P", args.password]
    return cmd + extra


def watch(args):
    state = load(args.state)
    if not state.get("started"):
        state["started"] = int(time.time())
    save(args.state, state)
    # -R drops retained messages, so everything seen here is a LIVE publish.
    cmd = sub_cmd(args, ["-R", "-F", "%t", "-t", "security/#", "-t", "esphome/#",
                         "-t", "homeassistant/#"])
    last_flush = 0.0
    while True:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                text=True, bufsize=1)
        for line in proc.stdout:
            topic = line.strip()
            ident = identity_of(topic)
            if ident:
                now = int(time.time())
                state["last_seen"][ident] = now
                state["counts"][ident] = state["counts"].get(ident, 0) + 1
            if time.time() - last_flush > 60:
                last_flush = time.time()
                save(args.state, state)
        proc.wait()
        save(args.state, state)
        time.sleep(5)              # broker restart / network blip -> resubscribe


def retained_snapshot(args, seconds=20):
    """Everything the broker replays to a fresh subscriber: the retained set."""
    out = subprocess.run(sub_cmd(args, ["-F", "%t", "-t", "#", "-W", str(seconds)]),
                         capture_output=True, text=True, timeout=seconds + 20)
    return [t for t in out.stdout.splitlines() if t.strip()]


def report(args):
    state = load(args.state)
    if not state.get("started"):
        sys.exit("no watch data yet -- start the watcher first")
    now = int(time.time())
    window_h = (now - state["started"]) / 3600.0

    retained, ha_by_ident, state_by_ident = retained_snapshot(args), {}, {}
    # First pass: learn every identity. The discovery match below needs the
    # complete set, so it cannot share a pass with building it.
    for topic in retained:
        ident = identity_of(topic)
        if ident:
            state_by_ident[ident] = state_by_ident.get(ident, 0) + 1
    known = set(state_by_ident) | set(state["last_seen"])
    # Longest name first: "ld2412_obyvak_stul" must not be credited to
    # "ld2412_obyvak", which is a different device and a prefix of it.
    by_len = sorted(known, key=lambda k: -len(k.split("/", 1)[1]))
    for topic in retained:
        parts = topic.split("/")
        if parts[0] == "homeassistant" and len(parts) >= 3:
            obj = parts[2]
            for ident in by_len:
                if obj.startswith(ident.split("/", 1)[1] + "_"):
                    ha_by_ident[ident] = ha_by_ident.get(ident, 0) + 1
                    break

    idents = sorted(set(list(state_by_ident) + list(state["last_seen"])))
    rows = []
    for ident in idents:
        seen = state["last_seen"].get(ident)
        rows.append((ident,
                     "never" if not seen else f"{(now - seen) / 3600.0:.1f} h ago",
                     0 if not seen else state["counts"].get(ident, 0),
                     state_by_ident.get(ident, 0),
                     ha_by_ident.get(ident, 0)))
    rows.sort(key=lambda r: (r[2] != 0, r[0]))     # silent identities first

    print(f"# MQTT identity activity -- watched {window_h:.1f} h "
          f"(since {time.strftime('%Y-%m-%d %H:%M', time.localtime(state['started']))})\n")
    print(f"{'identity':<34} {'last live publish':<18} {'msgs':>8} {'retained':>9} {'HA cfg':>7}")
    print("-" * 80)
    for ident, last, msgs, ret, ha in rows:
        print(f"{ident:<34} {last:<18} {msgs:>8} {ret:>9} {ha:>7}")
    silent = [r for r in rows if r[2] == 0]
    print(f"\n{len(silent)} identities published NOTHING in {window_h:.1f} h, "
          f"holding {sum(r[3] for r in silent)} retained + "
          f"{sum(r[4] for r in silent)} Home Assistant configs.")
    if window_h < 20:
        print("NOTE: window under 20 h -- too short to call anything dead yet.")
    print("\nThis is evidence, not a delete list. A node that is merely unplugged\n"
          "republishes its discovery on the next connect; decide per identity.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--user")
    ap.add_argument("--pass", dest="password")
    ap.add_argument("--state", default=DEFAULT_STATE)
    ap.add_argument("--report", action="store_true")
    args = ap.parse_args()
    report(args) if args.report else watch(args)


if __name__ == "__main__":
    main()
