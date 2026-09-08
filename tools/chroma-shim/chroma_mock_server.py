#!/usr/bin/env python3
"""
Proof-of-concept mock Razer Chroma SDK REST server.

Many "Chroma enabled" games (Dead Cells included) talk to a local Razer
Chroma SDK REST server on 127.0.0.1:54235 to send RGB lighting effect data.
On Linux there's normally no such server, so those calls just fail/timeout
silently. This script stands in for it, replies with plausible responses
so the game keeps talking, and prints every request (path + JSON body) to
the terminal so you can see exactly what data the game sends.

Implements the standard Chroma SDK REST flow:
  POST   /razer/chromasdk                       -> register app, get session
  POST   /razer/chromasdk/<id>/heartbeat         -> keepalive
  POST   /razer/chromasdk/<id>/<device>          -> create effect
  PUT    /razer/chromasdk/<id>/<device>/...      -> update effect data
  DELETE /razer/chromasdk/<id>                   -> unregister

Usage:
    python3 chroma_mock_server.py
    (then launch the game via wine, with its Chroma option enabled)
"""

import json
import uuid
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from datetime import datetime

HOST = "127.0.0.1"
PORT = 54235  # standard Razer Chroma SDK REST port

SESSIONS = {}
lock = threading.Lock()


def log(tag, data=None):
    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(f"\n[{ts}] {tag}")
    if data:
        try:
            print(json.dumps(data, indent=2))
        except TypeError:
            print(data)


class ChromaHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _read_json(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length) if length else b""
        if not raw:
            return {}
        try:
            return json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError:
            return {"_raw": raw.decode("utf-8", "replace")}

    def _send_json(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        parts = [p for p in self.path.split("/") if p]
        body = self._read_json()

        # App registration
        if self.path == "/razer/chromasdk":
            session_id = str(uuid.uuid4().int)[:10]
            with lock:
                SESSIONS[session_id] = body
            uri = f"http://{HOST}:{PORT}/razer/chromasdk/{session_id}"
            log("REGISTER app", body)
            self._send_json(200, {"result": 0, "sessionid": int(session_id), "uri": uri})
            return

        # Heartbeat
        if len(parts) >= 3 and parts[-1] == "heartbeat":
            log(f"HEARTBEAT session={parts[2]}")
            self._send_json(200, {"result": 0})
            return

        # Effect creation on a device endpoint (keyboard/mouse/mousepad/headset/keypad/chromalink)
        if len(parts) >= 4:
            device = parts[3]
            log(f"EFFECT create device={device} session={parts[2]}", body)
            self._send_json(200, {"result": 0, "effectId": str(uuid.uuid4())})
            return

        log(f"UNKNOWN POST {self.path}", body)
        self._send_json(200, {"result": 0})

    def do_PUT(self):
        body = self._read_json()
        log(f"PUT {self.path}", body)
        self._send_json(200, {"result": 0, "effectId": str(uuid.uuid4())})

    def do_DELETE(self):
        parts = [p for p in self.path.split("/") if p]
        if len(parts) >= 3:
            with lock:
                SESSIONS.pop(parts[2], None)
        log(f"UNREGISTER {self.path}")
        self._send_json(200, {"result": 0})

    def log_message(self, fmt, *args):
        pass  # silence default stderr access logging


if __name__ == "__main__":
    print(f"Mock Razer Chroma SDK REST server on http://{HOST}:{PORT}")
    print("Launch the game now (with Chroma enabled in its settings) and watch below.\n")
    server = ThreadingHTTPServer((HOST, PORT), ChromaHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
