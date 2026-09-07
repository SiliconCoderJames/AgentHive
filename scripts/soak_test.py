#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""加速浸泡测试：高频混合请求，观察稳定性（配合外部内存采样）。
用法: python scripts/soak_test.py [port] [seconds]
"""
import json
import os
import sys
import time
import urllib.request

BASE = "http://127.0.0.1:%s" % (sys.argv[1] if len(sys.argv) > 1 else "19090")
DURATION = int(sys.argv[2]) if len(sys.argv) > 2 else 90
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))

AGENT = "soak-agent-%d" % os.getpid()
KEY = ""


def http(method, path, agent=AGENT, key="", body=None):
    headers = {"Content-Type": "application/json", "X-Agent-Name": agent,
               "X-Api-Key": key or KEY}
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, headers=headers, method=method)
    try:
        with OPENER.open(req, timeout=10) as r:
            return r.status, json.loads(r.read())
    except Exception:
        return 0, {}


def register():
    st, body = http("POST", "/api/agents/register",
                    agent=None, key=None,
                    body={"name": AGENT, "role": "member"}, )
    return body["data"]["key"]


if __name__ == "__main__":
    master = os.environ.get("ZCODE_PLATFORM_MASTER_KEY", "")
    hdr = {"Content-Type": "application/json", "X-Master-Key": master}
    req = urllib.request.Request(BASE + "/api/agents/register",
                                 data=json.dumps({"name": AGENT}).encode(),
                                 headers=hdr, method="POST")
    KEY = json.loads(OPENER.open(req, timeout=10).read())["data"]["api_key"]

    ops, errors = 0, 0
    t0 = time.time()
    i = 0
    while time.time() - t0 < DURATION:
        i += 1
        try:
            which = i % 5
            if which == 0:
                http("POST", "/api/usage/report",
                     body={"tokens_in": 100, "tokens_out": 50,
                           "idempotency_key": "soak-%d" % (i // 5)})
            elif which == 1:
                http("GET", "/api/agents")
            elif which == 2:
                http("POST", "/api/knowledge/search",
                     body={"query": "soak test query %d" % i, "mode": "semantic"})
            elif which == 3:
                http("POST", "/api/agents/heartbeat", body={"current_task": "soak"})
            else:
                http("GET", "/api/memory?section=preference")
            ops += 1
        except Exception:
            errors += 1
        if i % 2000 == 0:
            print("  ...%d ops, %d errors, %.0fs" % (ops, errors, time.time() - t0))
            sys.stdout.flush()
    print("SOAK DONE: ops=%d errors=%d duration=%.0fs rate=%.0f ops/s"
          % (ops, errors, time.time() - t0, ops / max(1, time.time() - t0)))
