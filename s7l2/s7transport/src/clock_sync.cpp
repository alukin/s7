#include "s7/clock_sync.hpp"

#include <chrono>

#include <sys/timex.h>

namespace s7::clock {

auto read_kernel_sync() -> SyncStatus
{
    timex state{};
    const int result = ::ntp_adjtime(&state); // modes 0: read only
    SyncStatus status;
    status.source = "kernel";
    status.synchronized = result != -1 && result != TIME_ERROR && (state.status & STA_UNSYNC) == 0;
    status.max_error_ns = static_cast<std::int64_t>(state.maxerror) * 1000;
    status.estimated_error_ns = static_cast<std::int64_t>(state.esterror) * 1000;
    return status;
}

auto utc_now_ns() -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace s7::clock
