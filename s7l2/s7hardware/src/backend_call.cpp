// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/hardware_backend.hpp>
#if defined(__linux__)
#include <pthread.h>
#include <cerrno>
#endif
namespace s7::hardware {
BackendCallGuard::BackendCallGuard() noexcept {
#if defined(__linux__)
    const int saved_errno=errno;
    sigemptyset(&pipe_mask_);sigaddset(&pipe_mask_,SIGPIPE);
    active_=pthread_sigmask(SIG_BLOCK,&pipe_mask_,&old_mask_)==0;
    if(active_ && sigismember(&old_mask_,SIGPIPE)==0) {
        sigset_t pending{};
        drain_=sigpending(&pending)==0 && sigismember(&pending,SIGPIPE)==0;
    }
    errno=saved_errno;
#endif
}
BackendCallGuard::~BackendCallGuard() {
#if defined(__linux__)
    const int saved_errno=errno;
    if(active_) {
        if(drain_) { const timespec zero{};while(sigtimedwait(&pipe_mask_,nullptr,&zero)==SIGPIPE){} }
        pthread_sigmask(SIG_SETMASK,&old_mask_,nullptr);
    }
    errno=saved_errno;
#endif
}
} // namespace s7::hardware
