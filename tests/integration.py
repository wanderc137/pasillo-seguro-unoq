#!/usr/bin/env python3
"""Prueba de integracion del binario C con fuente sintetica."""
import json
import os
import signal
import subprocess
import sys
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = 18020
PHOTO_PORT = 18082
JPEG = b""


def get_json(url, timeout=4):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


def post(url, timeout=4):
    req = urllib.request.Request(url, method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


def wait_health(port, timeout=10):
    end = time.time() + timeout
    while time.time() < end:
        try:
            data = get_json(f"http://127.0.0.1:{port}/api/health", timeout=1)
            if data.get("ok"):
                return True
        except Exception:
            pass
        time.sleep(0.2)
    return False


class PhotoHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.split("?")[0] not in ("/photo.jpg", "/"):
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "image/jpeg")
        self.send_header("Content-Length", str(len(JPEG)))
        self.end_headers()
        self.wfile.write(JPEG)

    def log_message(self, *args):
        pass


def main():
    global JPEG
    binary = os.path.join(ROOT, "pasillo-seguro")
    if not os.path.isfile(binary):
        print("Falta ./pasillo-seguro; ejecuta make native", file=sys.stderr)
        return 2

    proc = subprocess.Popen(
        [binary, "--fake", "--port", str(PORT), "--web", "web"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    photo_srv = None
    try:
        assert wait_health(PORT), "el servidor no responde /api/health"
        print("integration: health OK")

        assert get_json(f"http://127.0.0.1:{PORT}/api/health")["ok"] is True
        st = get_json(f"http://127.0.0.1:{PORT}/api/status")
        assert st["ok"] is True
        assert st["state"] in ("idle", "running")

        post(f"http://127.0.0.1:{PORT}/api/start?url=fake&sensitivity=high")
        time.sleep(2.0)
        st = get_json(f"http://127.0.0.1:{PORT}/api/status")
        assert st["state"] == "running", st
        assert st["risk"] in ("green", "amber", "red"), st
        assert st["frames"] > 0, st
        print("integration: status", st["risk"], st["objects"], "objetos",
              round(st["fps"], 1), "fps")

        with urllib.request.urlopen(f"http://127.0.0.1:{PORT}/snapshot.jpg",
                                    timeout=4) as r:
            JPEG = r.read()
        assert JPEG[:2] == b"\xff\xd8", "snapshot no es JPEG"
        assert len(JPEG) > 1000, "snapshot demasiado chico"
        print("integration: snapshot", len(JPEG), "bytes")

        req = urllib.request.Request(f"http://127.0.0.1:{PORT}/stream.mjpg")
        with urllib.request.urlopen(req, timeout=4) as r:
            ctype = r.headers.get("Content-Type", "")
            assert "multipart/x-mixed-replace" in ctype, ctype
            chunk = r.read(8192)
            assert b"--frame" in chunk, "stream sin boundary"
            assert b"Content-Type: image/jpeg" in chunk
        print("integration: MJPEG OK")

        photo_srv = ThreadingHTTPServer(("127.0.0.1", PHOTO_PORT), PhotoHandler)
        threading.Thread(target=photo_srv.serve_forever, daemon=True).start()
        pr = get_json(
            f"http://127.0.0.1:{PORT}/api/probe?url="
            f"127.0.0.1:{PHOTO_PORT}"
        )
        assert pr["ok"] is True, pr
        assert pr["resolution"] == [480, 640], pr
        print("integration: probe OK", pr["resolution"])

        html = open(os.path.join(ROOT, "web", "index.html"), encoding="utf-8").read()
        for token in ("IP Webcam", "Iniciar", "Detener", 'id="stream"', "app.js"):
            assert token in html, token
        js = open(os.path.join(ROOT, "web", "app.js"), encoding="utf-8").read()
        assert "/stream.mjpg" in js, "app.js no usa el stream"
        assert "innerHTML" not in js, "app.js usa innerHTML"
        print("integration: UI estatica OK")
        print("integration: TODO OK")
        return 0
    finally:
        if photo_srv:
            photo_srv.shutdown()
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        out = proc.stdout.read() if proc.stdout else ""
        if proc.returncode not in (0, -15):
            print(out[-2000:], file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
