// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7TRANSPORT_INCLUDE_S7_CLOCK_SYNC_HPP
#define S7TRANSPORT_INCLUDE_S7_CLOCK_SYNC_HPP

// Whether this host's UTC clock is synchronized (NTP), so that its times can
// be compared with other nodes' (S7_Network_Wire_Protocol_v1.md, clock
// domain "utc").

#include <cstdint>
#include <string>

namespace s7::clock {

struct SyncStatus {
    bool synchronized{false};
    std::int64_t max_error_ns{};       // the kernel's worst-case error bound
    std::int64_t estimated_error_ns{}; // its estimate
    std::string source;                // "kernel" (ntp_adjtime) or "assumed" (node configuration)
};

// The kernel's NTP state: synchronized unless the clock is marked
// unsynchronized (STA_UNSYNC) or in error (TIME_ERROR). Never synchronized
// on Windows, which has no error bound to report.
[[nodiscard]] auto read_kernel_sync() -> SyncStatus;

// Synchronized within max_error_ns: the node's times are in the shared "utc" domain.
[[nodiscard]] inline bool usable(const SyncStatus& status, const std::int64_t max_error_ns)
{
    return status.synchronized && status.max_error_ns <= max_error_ns;
}

// Now, in nanoseconds since the Unix epoch (UTC).
[[nodiscard]] auto utc_now_ns() -> std::int64_t;

} // namespace s7::clock

#endif // S7TRANSPORT_INCLUDE_S7_CLOCK_SYNC_HPP
