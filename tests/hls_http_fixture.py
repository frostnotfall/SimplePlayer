"""Local HTTP/1.1 HLS fixture: growing live manifest and failed segment route."""
import argparse
import http.server
import json
import math
import pathlib
import time

parser = argparse.ArgumentParser()
parser.add_argument("--directory", required=True)
parser.add_argument("--port", type=int, default=18764)
args = parser.parse_args()
directory = pathlib.Path(args.directory).resolve()
lines = (directory / "index.m3u8").read_text().splitlines()
segments = [(float(lines[i][8:].rstrip(",")), lines[i + 1])
            for i in range(len(lines) - 1) if lines[i].startswith("#EXTINF:")]
route_started = {}
ports = set()
requests = {"manifest": 0, "segment": 0, "missing": 0}


class Handler(http.server.SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def __init__(self, *a, **kw):
        super().__init__(*a, directory=str(directory), **kw)

    def log_message(self, *a):
        pass

    def do_GET(self):
        ports.add(self.client_address[1])
        if self.path == "/metrics":
            payload = json.dumps(dict(requests, connections=len(ports))).encode()
        elif self.path in ("/live.m3u8", "/broken.m3u8"):
            requests["manifest"] += 1
            started = route_started.setdefault(self.path, time.monotonic())
            elapsed = time.monotonic() - started
            count = 2 if self.path == "/broken.m3u8" else min(len(segments), 4 + int(elapsed // 12) * 2)
            body = ["#EXTM3U", "#EXT-X-VERSION:3",
                    f"#EXT-X-TARGETDURATION:{math.ceil(max(d for d, _ in segments))}",
                    "#EXT-X-MEDIA-SEQUENCE:0"]
            for i, (duration, name) in enumerate(segments[:count]):
                body += [f"#EXTINF:{duration:.6f},",
                         "missing.ts" if self.path == "/broken.m3u8" and i == 1 else name]
            payload = ("\n".join(body) + "\n").encode()
        else:
            if self.path.endswith(".ts"):
                requests["segment"] += 1
                if self.path == "/missing.ts":
                    requests["missing"] += 1
            return super().do_GET()
        self.send_response(200)
        self.send_header("Content-Type", "application/vnd.apple.mpegurl")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
