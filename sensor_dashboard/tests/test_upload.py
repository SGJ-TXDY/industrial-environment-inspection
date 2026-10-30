#!/usr/bin/env python3
"""Runs build/test-upload against tests/fake_broker.py and checks what the
broker received. Usage: python3 tests/test_upload.py build/test-upload [workdir]
"""
import json
import os
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
binary = sys.argv[1]
work = sys.argv[2] if len(sys.argv) > 2 else "build"
broker = [sys.executable, os.path.join(here, "fake_broker.py")]


def run(mode, port, log, *extra):
    b = subprocess.Popen(broker + ["--port", str(port), "--log", log, "--duration", "45"] + list(extra))
    try:
        r = subprocess.run([binary, mode, str(port)], timeout=60)
        assert r.returncode == 0, mode + " failed"
    finally:
        b.kill()
        b.wait()
    with open(log, encoding="utf-8") as f:
        return [json.loads(line) for line in f]


got = run("flow", 18831, os.path.join(work, "broker-flow.log"), "--delay", "3")
topics = [m["topic"].split("/")[-1] for m in got]
# online, 3 queued records (oldest sensor overflowed), one more result,
# offline (pause), online + queued sensor (resume), online (reconnect), offline (exit)
assert topics == ["status", "inspection", "inspection", "alarm", "inspection",
                  "status", "status", "sensor", "status", "status"], topics
data = [json.loads(m["payload"]) for m in got]
assert data[0]["state"] == "online" and got[0]["retain"] == 1
assert data[1]["session"] == 5 and data[1]["seq"] == 1 and data[1]["result"] == "NORMAL" and data[1]["confidence"] == 97
assert data[2]["session"] is None and data[2]["seq"] == 2 and data[2]["result"] == "DEFECT" and data[2]["confidence"] is None
assert data[3] == {"ts": data[3]["ts"], "kind": "TEMP_HIGH", "state": "ON", "value": 41.2}
assert data[5]["state"] == "offline" and data[6]["state"] == "online"
assert data[7] == {"ts": data[7]["ts"], "temperature": 26.125}
assert data[8]["state"] == "online" and data[9]["state"] == "offline"
assert all(m["qos"] == 1 and m["topic"].startswith("test/dash/") for m in got)
print("PASS broker side: topics in order, JSON fields, retained status, sensor overflow dropped")

run("auth", 18832, os.path.join(work, "broker-auth.log"), "--password", "right")
print("PASS broker side: wrong password refused")
