"""RX-only native lifecycle probe. Run in a disposable subprocess with a timeout.

Bypasses Qt/Soapy/application DSP. Uses an explicitly selected RTL DLL and its
adjacent dependencies. Requires exclusive tuner access. No saved settings change.
"""
import argparse
import ctypes as c
import json
import os
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--dll", type=Path, required=True)
    p.add_argument("--cycles", type=int, default=5)
    args = p.parse_args()
    if not args.dll.is_file() or not 1 <= args.cycles <= 20:
        p.error("Require an existing DLL and 1..20 cycles")
    with os.add_dll_directory(str(args.dll.resolve().parent)):
        dll = c.CDLL(str(args.dll.resolve()))
        callback_type = c.CFUNCTYPE(None, c.POINTER(c.c_ubyte), c.c_uint32, c.c_void_p)
        signatures = {
            "rtlsdr_get_device_count": ([], c.c_uint32),
            "rtlsdr_open": ([c.POINTER(c.c_void_p), c.c_uint32], c.c_int),
            "rtlsdr_close": ([c.c_void_p], c.c_int),
            "rtlsdr_set_sample_rate": ([c.c_void_p, c.c_uint32], c.c_int),
            "rtlsdr_set_center_freq": ([c.c_void_p, c.c_uint32], c.c_int),
            "rtlsdr_set_tuner_gain_mode": ([c.c_void_p, c.c_int], c.c_int),
            "rtlsdr_set_tuner_gain": ([c.c_void_p, c.c_int], c.c_int),
            "rtlsdr_reset_buffer": ([c.c_void_p], c.c_int),
            "rtlsdr_cancel_async": ([c.c_void_p], c.c_int),
            "rtlsdr_read_async": ([c.c_void_p, callback_type, c.c_void_p, c.c_uint32, c.c_uint32], c.c_int),
        }
        for name, (types, result) in signatures.items():
            function = getattr(dll, name)
            function.argtypes, function.restype = types, result
        if dll.rtlsdr_get_device_count() != 1:
            p.error("Probe requires exactly one available RTL device")
        for cycle in range(args.cycles):
            handle = c.c_void_p()
            print(json.dumps({"cycle": cycle, "stage": "open"}), flush=True)
            if dll.rtlsdr_open(c.byref(handle), 0) != 0:
                raise RuntimeError("RTL open failed")
            try:
                for name, value in (("rtlsdr_set_sample_rate", 2048000),
                                    ("rtlsdr_set_center_freq", 98100000),
                                    ("rtlsdr_set_tuner_gain_mode", 1),
                                    ("rtlsdr_set_tuner_gain", 200)):
                    if getattr(dll, name)(handle, value) != 0:
                        raise RuntimeError(name + " failed")
                if dll.rtlsdr_reset_buffer(handle) != 0:
                    raise RuntimeError("reset buffer failed")
                blocks = 0
                cancel_result = None
                @callback_type
                def receive(_data, _size, _ctx):
                    nonlocal blocks, cancel_result
                    blocks += 1
                    if blocks == 32:
                        cancel_result = dll.rtlsdr_cancel_async(handle)
                print(json.dumps({"cycle": cycle, "stage": "read_async"}), flush=True)
                result = dll.rtlsdr_read_async(handle, receive, None, 15, 32768)
                if result != 0 or blocks < 32 or cancel_result != 0:
                    raise RuntimeError(f"read/cancel failed: {result}, {blocks}, {cancel_result}")
            finally:
                print(json.dumps({"cycle": cycle, "stage": "close"}), flush=True)
                close_result = dll.rtlsdr_close(handle)
            if close_result != 0:
                raise RuntimeError("RTL close failed")
            print(json.dumps({"cycle": cycle, "stage": "closed", "blocks": blocks}), flush=True)


if __name__ == "__main__":
    main()
