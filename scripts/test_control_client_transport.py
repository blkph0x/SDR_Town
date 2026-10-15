"""Exercise the shipped DLL against bounded local HTTP fault fixtures, never RF."""
import argparse
import ctypes
import json
from pathlib import Path
import socket
import threading
import time


class Config(ctypes.Structure):
    _fields_ = [("host", ctypes.c_char_p), ("port", ctypes.c_uint16),
                ("token", ctypes.c_char_p), ("timeoutMs", ctypes.c_uint32)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", type=Path, required=True)
    args = parser.parse_args()
    dll = ctypes.CDLL(str(args.dll.resolve()))
    request = dll.SdrTownControl_Request
    request.argtypes = [ctypes.POINTER(Config), ctypes.c_char_p, ctypes.c_char_p,
                        ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t]
    request.restype = ctypes.c_int

    tested = 0

    def run(payload, capacity=262144, drip=False):
        nonlocal tested
        tested += 1
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0)); listener.listen(1); listener.settimeout(5)
            stopped = threading.Event()
            def serve():
                with listener.accept()[0] as client:
                    client.settimeout(3)
                    client.recv(8192)
                    try:
                        if drip:
                            for char in payload:
                                if stopped.wait(.02): break
                                client.sendall(bytes([char]))
                        else:
                            client.sendall(payload)
                    except OSError:
                        pass  # Expected when the DLL rejects declared oversized content.
            worker = threading.Thread(target=serve)
            worker.start()
            buffer = ctypes.create_string_buffer(capacity)
            config = Config(b"127.0.0.1", listener.getsockname()[1], b"fixture", 250)
            start = time.monotonic()
            result = request(ctypes.byref(config), b"GET", b"/v1/status", b"{}", buffer, capacity)
            elapsed = time.monotonic() - start
            stopped.set(); worker.join(5)
            assert not worker.is_alive()
            return result, json.loads(buffer.value), elapsed

    def framed(body, extra=b"", length=None):
        return (b"HTTP/1.1 200 OK\r\nContent-Length: " + str(len(body) if length is None else length).encode()
                + b"\r\n" + extra + b"\r\n" + body)

    code, body, _ = run(framed(b'{"ok":true}'))
    assert code == 0 and body["ok"]
    code, body, _ = run(framed(json.dumps({"ok": True, "data": "x" * 261900}).encode()))
    assert code == 0 and len(body["data"]) == 261900
    code, body, _ = run(framed(b'{"ok":true,"status":200,"stateOmitted":true}'))
    assert code == 0 and body["stateOmitted"]
    code, body, _ = run(b'HTTP/1.0 200 OK\r\ncOnTeNt-LeNgTh:\t11 \t\r\nX-Valid_Field: value\r\n\r\n{"ok":true}')
    assert code == 0 and body["ok"]
    cases = [framed(b'{"ok":true}', length=100),
             framed(b'{"ok":true}', b"Content-Length: 11\r\n"),
             framed(b'{"ok":true}', b"Transfer-Encoding: chunked\r\n"),
             framed(b'{"ok":true,broken}'),
             framed(b'{"ok":false,"nested":{"ok":true}}'),
             framed(b'{}', length=300000)]
    for status in (b"HTTP/not-http 200 OK", b"HTTP/1.1 200garbage", b"HTTP/1.1 20 OK",
                   b"HTTP/1.1 2000 OK", b"HTTP/1.1 +200 OK", b"HTTP/1.1 200",
                   b"HTTP/1.1 600 Invalid", b"HTTP/1.1 200 bad\x00reason"):
        cases.append(status + b'\r\nContent-Length: 11\r\n\r\n{"ok":true}')
    for header in (b"Transfer-Encoding : chunked", b"Transfer-Encoding\t: chunked",
                   b": invalid", b"Bad Header: value", b"Bad(Header): value",
                   b"X-Test: bad\x00value", b"X-Test: bad\rvalue", b" X-Fold: value",
                   b"Content-Length: +11", b"Content-Length: 11, 11"):
        cases.append(framed(b'{"ok":true}', header + b"\r\n"))
    cases.append(framed(b'{"ok":true}', b"X-Fill: " + b"x" * 16384 + b"\r\n"))
    cases.append(b'HTTP/1.1 200 OK\r\nX-No-Length: yes\r\n\r\n{"ok":true}')
    cases.append(framed(b'{"ok":true}') + b'extra')
    for payload in cases:
        code, body, _ = run(payload)
        assert code == -4 and not body["ok"], (code, body)
    code, body, _ = run(framed(json.dumps({"ok": True, "data": "x" * 1000}).encode()), capacity=128)
    assert code == -4 and body["errorCode"] == "response_too_large"
    code, body, elapsed = run(framed(b'{"ok":true}'), drip=True)
    assert code == -4 and not body["ok"] and elapsed < 2, (code, body, elapsed)
    print(f"PASS: DLL strict HTTP grammar, complete framing, bounded acknowledgments, exact body, "
          f"oversize/small-buffer, JSON truth, and absolute receive deadline ({tested} cases)")


if __name__ == "__main__":
    main()
