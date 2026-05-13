"""Pure-ctypes DPAPI wrapper.

We don't go through pywin32 because PyInstaller's ``--onefile`` packing
of ``win32crypt`` requires the right combination of hidden imports +
pywintypes/pythoncom DLLs + a working ``pywin32_postinstall``. ctypes
just calls ``crypt32.dll`` directly -- compatible blob format with
.NET's ``ProtectedData``.
"""

from __future__ import annotations

import ctypes
import sys
from ctypes import wintypes


CRYPTPROTECT_LOCAL_MACHINE = 0x4


class _DataBlob(ctypes.Structure):
    _fields_ = [("cbData", wintypes.DWORD), ("pbData", ctypes.POINTER(ctypes.c_byte))]


def _call(name: str, in_blob: bytes, flags: int) -> bytes:
    if sys.platform != "win32":
        raise RuntimeError("DPAPI is only available on Windows")
    crypt32 = ctypes.WinDLL("crypt32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

    in_buf = (ctypes.c_byte * len(in_blob)).from_buffer_copy(in_blob)
    in_data = _DataBlob(len(in_blob), in_buf)
    out_data = _DataBlob()

    fn = getattr(crypt32, name)
    fn.argtypes = [
        ctypes.POINTER(_DataBlob),
        wintypes.LPWSTR,
        ctypes.POINTER(_DataBlob),
        ctypes.c_void_p,
        ctypes.c_void_p,
        wintypes.DWORD,
        ctypes.POINTER(_DataBlob),
    ]
    fn.restype = wintypes.BOOL

    ok = fn(ctypes.byref(in_data), None, None, None, None, flags,
            ctypes.byref(out_data))
    if not ok:
        err = ctypes.get_last_error()
        raise OSError(err, f"{name} failed (Win32 error {err})")
    try:
        result = bytes(
            (ctypes.c_byte * out_data.cbData).from_address(
                ctypes.addressof(out_data.pbData.contents)
            )
        )
    finally:
        kernel32.LocalFree(out_data.pbData)
    return result


def protect(plaintext: bytes) -> bytes:
    return _call("CryptProtectData", plaintext, CRYPTPROTECT_LOCAL_MACHINE)


def unprotect(blob: bytes) -> bytes:
    return _call("CryptUnprotectData", blob, 0)
