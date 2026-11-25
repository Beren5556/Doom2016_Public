"""Common loopback host factory. Consumers supply transport and ui_root."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import secrets
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parent

def make_server(transport, port=0, surface=None, ui_root=None):
    # A consumer supplies its own parameter view without changing XR/input code.
    ui_root=Path(ui_root) if ui_root is not None else ROOT/'web'
    token=secrets.token_urlsafe(32)
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args): pass
        def send(self,status,data,kind='application/json'):
            if not isinstance(data,bytes): data=json.dumps(data).encode()
            self.send_response(status)
            self.send_header('Content-Type',kind)
            self.send_header('Content-Length',str(len(data)))
            self.send_header('Cache-Control','no-store')
            self.send_header('X-Content-Type-Options','nosniff')
            self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'; connect-src 'self'")
            try:self.end_headers();self.wfile.write(data)
            except ConnectionError:pass  # Client closed; never repeat its command.
        def valid_host(self):
            return self.headers.get('Host')==f'127.0.0.1:{self.server.server_port}'
        def do_GET(self):
            if not self.valid_host(): return self.send(403,{'error':'Invalid host'})
            if self.path=='/?token='+token:
                html=(ui_root/'index.html').read_text(encoding='utf-8').replace('__TOKEN__',token)
                return self.send(200,html.encode(),'text/html; charset=utf-8')
            if self.path=='/information.js': return self.send(200,(ROOT/'web/information.js').read_bytes(),'text/javascript')
            if self.path=='/navigation.js': return self.send(200,(ROOT/'web/navigation.js').read_bytes(),'text/javascript')
            if self.path=='/resolution.js': return self.send(200,(ROOT/'web/resolution.js').read_bytes(),'text/javascript')
            if self.path=='/adapter.js': return self.send(200,(ui_root/'adapter.js').read_bytes(),'text/javascript')
            return self.send(404,{'error':'Not found'})
        def do_POST(self):
            origin=f'http://127.0.0.1:{self.server.server_port}'
            if not self.valid_host() or self.headers.get('X-Overlay-Token')!=token or self.headers.get('Origin') not in (None,origin):
                return self.send(403,{'error':'Invalid origin or token'})
            try:
                size=int(self.headers.get('Content-Length','0'))
                if self.path=='/xr/frame' and surface:
                    if not 0<size<=8*1024*1024: raise ValueError('Invalid PNG size')
                    surface.frame(self.rfile.read(size));return self.send(200,{'ok':True})
                if self.path=='/xr/info' and surface:
                    return self.send(200,surface.information())
                if self.path=='/xr/resolution' and surface:
                    return self.send(200,surface.resolution())
                if self.path=='/output/state':
                    result=transport.exchange({'op':'output_state'})
                    if not isinstance(result,dict):raise ValueError('Invalid output adapter response')
                    if surface:result={**result,'submittedViews':surface.resolution()}
                    return self.send(200,result)
                if self.path=='/output/apply':
                    if not 0<size<=16000:raise ValueError('Invalid output request size')
                    request=json.loads(self.rfile.read(size));request['op']='output_apply'
                    return self.send(200,transport.exchange(request))
                if self.path=='/xr/input' and surface:
                    return self.send(200,surface.input())
                if self.path=='/xr/close' and surface:
                    return self.send(200,surface.close_overlay())
                if self.path!='/api': return self.send(404,{'error':'Not found'})
                if not 0<size<=16000: raise ValueError('Invalid request size')
                result=transport.exchange(json.loads(self.rfile.read(size)))
                self.send(200,result)
            except Exception as error: self.send(503,{'error':str(error)})
    server=ThreadingHTTPServer(('127.0.0.1',port),Handler)
    server.daemon_threads=True
    return server,f'http://127.0.0.1:{server.server_port}/?token={token}'

