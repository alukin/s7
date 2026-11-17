// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#if defined(__linux__)
#include <signal.h>
#endif
namespace s7::hardware {
// On Linux some device libraries write sockets without suppressing SIGPIPE. Block it
// only on the calling thread during backend work, then consume a newly raised
// signal before restoring that thread's mask. Never change process disposition
// or consume a signal that the caller already had blocked/pending. Backend-owned
// threads created inside this scope inherit the blocked mask.
// Other platforms are a no-op and require separate backend qualification.
class BackendCallGuard {
public:
    BackendCallGuard() noexcept;
    ~BackendCallGuard();
    BackendCallGuard(const BackendCallGuard&)=delete;
    BackendCallGuard& operator=(const BackendCallGuard&)=delete;
private:
#if defined(__linux__)
    sigset_t old_mask_{},pipe_mask_{};
    bool active_{},drain_{};
#endif
};
} // namespace s7::hardware
