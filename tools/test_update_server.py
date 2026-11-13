#!/usr/bin/env python3
"""test_update_server.py <file> <port>: answers the in-app update's tests (tools/test_update.cpp) as GitHub's servers
would: the file whole with its size, in chunks, through redirects (absolute and relative), a 404, endless redirects."""
import http.server
import sys

DATA = open(sys.argv[1], "rb").read()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def send(self, code, headers, body=b""):
        self.send_response(code)
        for k, v in headers:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        p = self.path
        if p == "/sized":
            self.send(200, [("Content-Length", str(len(DATA)))], DATA)
        elif p in ("/chunked", "/chunked-2"):
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            at, size = 0, 1
            while at < len(DATA):
                part = DATA[at:at + size]
                self.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
                at += len(part)
                size = size * 3 + 7 if size < 70000 else 1000
            self.wfile.write(b"0\r\n\r\n")
        elif p == "/redirect":
            self.send(302, [("Location", "https://objects.example.test/sized"), ("Content-Length", "0")])
        elif p == "/redirect-relative":
            self.send(301, [("Location", "/sized"), ("Content-Length", "0")])
        elif p == "/redirect-chunked":
            self.send(302, [("Location", "https://release-assets.example.test/chunked-2"), ("Content-Length", "0")])
        elif p == "/loop":
            self.send(302, [("Location", "/loop"), ("Content-Length", "0")])
        else:
            self.send(404, [("Content-Length", "9")], b"not found")
        self.close_connection = True


http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[2])), Handler).serve_forever()
