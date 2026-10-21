# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""UTC clock and its synchronization state (S7_Control_Protocol_MVP.md 5.2, Clock)."""
import ctypes
import ctypes.util
import sys
import time
from dataclasses import dataclass


@dataclass
class SyncStatus:
    synchronized: bool = False
    max_error_ns: int = 0
    estimated_error_ns: int = 0
    source: str = "kernel"


def utc_now_ns() -> int:
    return time.time_ns()


class _Timex(ctypes.Structure):
    # struct timex (Linux, 64-bit layout of <sys/timex.h>)
    _fields_ = [
        ("modes", ctypes.c_uint),
        ("offset", ctypes.c_long),
        ("freq", ctypes.c_long),
        ("maxerror", ctypes.c_long),
        ("esterror", ctypes.c_long),
        ("status", ctypes.c_int),
        ("constant", ctypes.c_long),
        ("precision", ctypes.c_long),
        ("tolerance", ctypes.c_long),
        ("time_sec", ctypes.c_long),
        ("time_usec", ctypes.c_long),
        ("tick", ctypes.c_long),
        ("ppsfreq", ctypes.c_long),
        ("jitter", ctypes.c_long),
        ("shift", ctypes.c_int),
        ("stabil", ctypes.c_long),
        ("jitcnt", ctypes.c_long),
        ("calcnt", ctypes.c_long),
        ("errcnt", ctypes.c_long),
        ("stbcnt", ctypes.c_long),
        ("tai", ctypes.c_int),
        ("padding", ctypes.c_int * 11),
    ]


_STA_UNSYNC = 0x0040
_TIME_ERROR = 5


def read_kernel_sync() -> SyncStatus:
    """The kernel's view (ntp_adjtime, read only); unsynchronized where it cannot be read."""
    if not sys.platform.startswith("linux"):
        return SyncStatus()
    try:
        libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
        state = _Timex()
        result = libc.ntp_adjtime(ctypes.byref(state))
    except (OSError, AttributeError):
        return SyncStatus()
    return SyncStatus(
        synchronized=result not in (-1, _TIME_ERROR) and not (state.status & _STA_UNSYNC),
        max_error_ns=state.maxerror * 1000,
        estimated_error_ns=state.esterror * 1000,
    )


class Clock:
    def __init__(self, max_error_ms: float = 250.0, assume_synchronized: bool = False):
        if not max_error_ms > 0:
            raise ValueError("clock.max_error_ms must be a positive number")
        self.max_error_bound_ns = int(max_error_ms * 1e6)
        self.assume_synchronized = assume_synchronized

    def sync(self) -> SyncStatus:
        if self.assume_synchronized:
            return SyncStatus(synchronized=True, source="assumed")
        return read_kernel_sync()

    def usable(self, status: SyncStatus) -> bool:
        return status.synchronized and status.max_error_ns <= self.max_error_bound_ns
