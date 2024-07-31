# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from http.server import BaseHTTPRequestHandler, HTTPServer
from threading import Thread

import requests
import socket


class RDBRequestHandler(BaseHTTPRequestHandler):

  def do_POST(self):
    content_length = int(self.headers['Content-Length'])
    post_data = self.rfile.read(content_length)
    self.server.register_post_data(post_data)
    self.send_response(requests.codes.ok)
    self.end_headers()
    return


def get_free_port():
  s = socket.socket(socket.AF_INET, type=socket.SOCK_STREAM)
  s.bind(('localhost', 0))
  _, port = s.getsockname()
  s.close()
  return port


class RDBMockServer(HTTPServer):

  def __init__(self) -> None:
    self.port = get_free_port()
    super().__init__(('localhost', self.port), RDBRequestHandler)
    self.thread = Thread(target=self.serve_forever)
    self.thread.daemon = True
    self.thread.start()
    self.post_data = []

  def register_post_data(self, data):
    self.post_data.append(data)

  @property
  def address(self):
    return f'localhost:{self.port}'
