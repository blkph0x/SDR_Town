"""Actual DLL: implicit pairing never follows arbitrary host names; overrides win.

Run after a normal Town startup has provisioned the current-user credential.
The fixture never logs, saves or returns the received credential.
"""
import argparse
import ctypes as c
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading


class Config(c.Structure):
    _fields_ = [('host',c.c_char_p),('port',c.c_uint16),('token',c.c_char_p),('timeout',c.c_uint32)]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll',type=Path,required=True)
    args=parser.parse_args()
    checks=[]
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args): pass
        def do_GET(self):
            auth=self.headers.get('Authorization','')
            checks.append((auth.startswith('Bearer ') and len(auth)==71,
                           auth=='Bearer fixture-explicit',not auth))
            body=b'{"ok":true}'
            self.send_response(200)
            self.send_header('Content-Length',str(len(body)))
            self.end_headers();self.wfile.write(body)
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever)
    thread.start()
    try:
        dll=c.CDLL(str(args.dll.resolve()))
        call=dll.SdrTownControl_Status
        call.argtypes=[c.POINTER(Config),c.c_char_p,c.c_size_t]
        call.restype=c.c_int
        for host,token in ((b'127.0.0.1',None),(b'localhost',None),(b'127.0.0.1',b'fixture-explicit')):
            cfg=Config(host,server.server_port,token,2500)
            buf=c.create_string_buffer(1024)
            assert call(c.byref(cfg),buf,len(buf))==0,'DLL fixture request failed'
        assert checks==[(True,False,False),(False,False,True),(False,True,False)],'Credential routing mismatch'
    finally:
        server.shutdown();thread.join();server.server_close()
    print('PASS: literal-loopback-only implicit pairing and explicit override; no secret logged')


if __name__=='__main__': main()
