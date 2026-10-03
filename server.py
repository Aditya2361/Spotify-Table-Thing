import hmac
import io
import json
import subprocess
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import config

PORT = 8000


COMMANDS = {
    "toggle": "toggle-play-pause",
    "next": "next-track",
    "previous": "previous-track",
}

_lock = threading.Lock()  
_state = {
    "active": False, "title": "", "artist": "",
    "elapsed": 0, "duration": 0, "playing": False,
    "has_cover": False, "cover_id": 0,
}
_cover_raw = b""
_cover_png = b""
_last_cover = None


def to_rgb565(img):
    
    data = img.convert("RGB").tobytes()
    out = bytearray()
    for i in range(0, len(data), 3):
        r, g, b = data[i], data[i + 1], data[i + 2]
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out.append(value >> 8)
        out.append(value & 0xFF)
    return bytes(out)


def update(song, cover):
    global _cover_raw, _cover_png, _last_cover
    with _lock:
        _state.update(
            active=True,
            title=song["title"],
            artist=song["artist"],
            elapsed=round(song["elapsed"], 1),
            duration=round(song["duration"], 1),
            playing=song["playing"],
        )
        if cover is not _last_cover:        
            _last_cover = cover
            if cover is None:
                _cover_raw, _cover_png = b"", b""
                _state["has_cover"] = False
            else:
                _cover_raw = to_rgb565(cover)
                buf = io.BytesIO()
                cover.save(buf, "PNG")
                _cover_png = buf.getvalue()
                _state["has_cover"] = True
            _state["cover_id"] += 1


def set_idle():
    with _lock:
        _state["active"] = False


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, content_type, body):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _check(self):
        
        parsed = urllib.parse.urlparse(self.path)
        given = urllib.parse.parse_qs(parsed.query).get("key", [""])[0]
        if not hmac.compare_digest(given.encode(), config.ACCESS_KEY.encode()):
            self._send(401, "text/plain", b"unauthorized")
            return None
        return parsed.path

    def do_GET(self):
        path = self._check()
        if path is None:
            return
        with _lock:
            state_json = json.dumps(_state).encode()
            raw, png = _cover_raw, _cover_png
        if path == "/state":
            self._send(200, "application/json", state_json)
        elif path == "/cover.raw" and raw:
            self._send(200, "application/octet-stream", raw)
        elif path == "/cover.png" and png:
            self._send(200, "image/png", png)
        else:
            self._send(404, "text/plain", b"not found")

    def do_POST(self):
        path = self._check()
        if path is None:
            return
        name = path.removeprefix("/cmd/")
        if path.startswith("/cmd/") and name in COMMANDS:
            subprocess.run(["media-control", COMMANDS[name]], capture_output=True)
            self._send(200, "text/plain", b"ok")
        else:
            self._send(404, "text/plain", b"not found")

    def log_message(self, *args):
        pass    


def start():
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print(f"Server running on port {PORT}")