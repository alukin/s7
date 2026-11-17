// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <s7/hardware_backend.hpp>
#include <cassert>
#if defined(__linux__)
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <cerrno>
namespace {
int broken_socket() {int sockets[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);close(sockets[1]);return sockets[0];}
template<class F> int child(F function) {
    const auto pid=fork();assert(pid>=0);
    if(!pid) {signal(SIGPIPE,SIG_DFL);sigset_t mask;sigemptyset(&mask);pthread_sigmask(SIG_SETMASK,&mask,nullptr);function();_exit(0);}
    int status;assert(waitpid(pid,&status,0)==pid);return status;
}
}
#endif
int main() {
#if defined(__linux__)
    auto negative=child([]{int fd=broken_socket();static_cast<void>(write(fd,"x",1));});
    assert(WIFSIGNALED(negative) && WTERMSIG(negative)==SIGPIPE);
    auto protected_call=child([] {
        int fd=broken_socket();
        {s7::hardware::BackendCallGuard guard;assert(write(fd,"x",1)==-1 && errno==EPIPE);}
        assert(errno==EPIPE);sigset_t mask,pending;pthread_sigmask(SIG_BLOCK,nullptr,&mask);sigpending(&pending);
        assert(!sigismember(&mask,SIGPIPE) && !sigismember(&pending,SIGPIPE));
        struct sigaction action{};sigaction(SIGPIPE,nullptr,&action);assert(action.sa_handler==SIG_DFL);close(fd);
    });
    assert(WIFEXITED(protected_call) && WEXITSTATUS(protected_call)==0);
    auto caller_policy=child([] {
        sigset_t mask;sigemptyset(&mask);sigaddset(&mask,SIGPIPE);pthread_sigmask(SIG_BLOCK,&mask,nullptr);
        raise(SIGPIPE);int fd=broken_socket();
        {s7::hardware::BackendCallGuard guard;assert(write(fd,"x",1)==-1 && errno==EPIPE);}
        sigset_t pending,current;sigpending(&pending);pthread_sigmask(SIG_BLOCK,nullptr,&current);
        assert(sigismember(&pending,SIGPIPE) && sigismember(&current,SIGPIPE));close(fd);
    });
    assert(WIFEXITED(caller_policy) && WEXITSTATUS(caller_policy)==0);
#endif
}
