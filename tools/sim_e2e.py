#!/usr/bin/env python3
"""End-to-end test in kode-sim: plug the simulated PC into the dock port, save a profile through
the web editor, run it, and read what the PC typed.

  kode-sim run --no-open --port 8767 --forward 18081:80 <this project>
  python3 tools/sim_e2e.py [page_port] [editor_port]
"""
import asyncio
import json
import sys
import urllib.request

import aiohttp

PAGE = int(sys.argv[1]) if len(sys.argv) > 1 else 8767
WEB = int(sys.argv[2]) if len(sys.argv) > 2 else 18081
BASE = f"http://127.0.0.1:{WEB}"

PROFILE = """title E2E test
arm plug
gap 100
spam F10 2
type Hi!
line $who
keys TAB ENTER
"""


def http(path, body=None):
    req = urllib.request.Request(BASE + path, data=body.encode() if body is not None else None,
                                 method="POST" if body is not None else "GET")
    with urllib.request.urlopen(req, timeout=20) as r:
        t = r.read().decode()
        return json.loads(t) if t.startswith(("{", "[")) else t


async def main():
    ok = True
    usb = {}
    async with aiohttp.ClientSession() as s:
        async with s.ws_connect(f"http://127.0.0.1:{PAGE}/ws") as ws:
            async def pump(sec):
                end = asyncio.get_event_loop().time() + sec
                while asyncio.get_event_loop().time() < end:
                    try:
                        m = await ws.receive(timeout=0.5)
                    except asyncio.TimeoutError:
                        continue
                    if m.type == aiohttp.WSMsgType.TEXT:
                        try:
                            j = json.loads(m.data)
                        except ValueError:
                            continue
                        if isinstance(j, dict) and "usb" in j:
                            usb.update(j["usb"])

            await ws.send_str(json.dumps({"usb": {"cmd": "layout", "layout": "us"}}))
            await ws.send_str(json.dumps({"usb": {"cmd": "clear_typed"}}))
            # a profile with an error is reported, by line
            chk = http("/api/check", "title X\nkeys F10 NOPE\n")
            print("check:", chk["error_count"], chk["errors"])
            ok &= chk["error_count"] == 1 and chk["errors"][0]["line"] == 2
            # save one to the card
            sv = http("/api/save?name=e2e-test", PROFILE.replace("$who", "ok"))
            print("save:", sv.get("saved"), sv.get("rc"), sv.get("error_count"))
            ok &= bool(sv.get("saved"))
            names = [p["name"] for p in http("/api/profiles")]
            print("profiles:", names)
            ok &= "e2e-test" in names
            http("/api/cmd", "select e2e-test")
            await pump(1)
            http("/api/cmd", "start")
            await pump(1.5)
            st = http("/api/state")
            print("after start:", st["state"], st["title"])
            ok &= st["state"] == "armed"
            # the PC comes up: plug the cable in
            await ws.send_str(json.dumps({"usb": {"cmd": "plug"}}))
            for _ in range(60):
                await pump(1)
                st = http("/api/state")
                if st["state"] == "done":
                    break
            print("state:", st["state"], "runs", st["runs"])
            await pump(2)
            typed = usb.get("typed", "")
            print("usb stage:", usb.get("stage"), "device:", (usb.get("device") or {}).get("product"))
            print("typed:", repr(typed))
            ok &= st["state"] == "done" and "Hi!" in typed and "ok" in typed
            # the remote keyboard
            await ws.send_str(json.dumps({"usb": {"cmd": "clear_typed"}}))
            http("/api/cmd", "type abc")
            await pump(3)
            print("remote typed:", repr(usb.get("typed", "")))
            ok &= "abc" in usb.get("typed", "")
            http("/api/delete?name=e2e-test", "")
            await ws.send_str(json.dumps({"usb": {"cmd": "unplug"}}))
            await pump(1)
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


sys.exit(asyncio.run(main()))
