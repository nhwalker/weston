"""A minimal fake WebDriver endpoint used to test nested delegation.

Responds like a child driver (e.g. ChromeDriver): creates a session and
answers a couple of element/navigation commands with canned values.

Usage: python3 mock_child_driver.py PORT
"""

import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def _reply(self, payload, status=200):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path == "/session":
            self._reply({"value": {"sessionId": "mock-child-session",
                                   "capabilities": {}}})
        elif self.path.endswith("/element"):
            self._reply({"value": {
                "element-6066-11e4-a52e-4f735466cecf": "mock-element-1"}})
        else:
            self._reply({"value": None})

    def do_GET(self):
        if self.path.endswith("/url"):
            self._reply({"value": "https://mock.example/page"})
        elif self.path.endswith("/source"):
            self._reply({"value": "<html>mock</html>"})
        else:
            self._reply({"value": None})

    def do_DELETE(self):
        self._reply({"value": None})

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
