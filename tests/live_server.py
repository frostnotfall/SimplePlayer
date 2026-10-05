"""Private, paced FLV fixture. Runs only in the live timeshift integration test."""
import http.server
import subprocess
import sys

ffmpeg, media = sys.argv[1:3]

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != '/live.flv':
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Type', 'video/x-flv')
        self.end_headers()
        process = subprocess.Popen([ffmpeg, '-hide_banner', '-loglevel', 'error', '-nostdin', '-re',
            '-stream_loop', '-1', '-i', media, '-c', 'copy', '-flvflags', 'no_duration_filesize',
            '-flush_packets', '1', '-f', 'flv', 'pipe:1'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        try:
            while chunk := process.stdout.read1(16384):
                self.wfile.write(chunk)
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            process.kill()
            process.wait()

    def log_message(self, *args):
        pass

http.server.ThreadingHTTPServer(('127.0.0.1', 18765), Handler).serve_forever()
