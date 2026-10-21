// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "atomic_file.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#if defined(__linux__)
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
auto read(const QString& path) -> QByteArray
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "read destination");
    return file.readAll();
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir dir;
        require(dir.isValid(), "temporary directory");
        const auto path = dir.filePath("model.s7diagram");
        const QByteArray original("{\"model\":\"original\"}\n");
        const QByteArray replacement("{\"model\":\"replacement\"}\n");
        QString error;
        require(s7studio::writeFileAtomically(path, original, error), "initial save");
        require(read(path) == original, "initial contents");
        require(s7studio::writeFileAtomically(path, replacement, error), "overwrite");
        require(read(path) == replacement && error.isEmpty(), "replacement contents");
        require(!s7studio::writeFileAtomically(dir.path(), original, error), "directory rejected");
        require(!error.isEmpty() && read(path) == replacement, "open failure preserves file");
        require(!s7studio::writeFileAtomically(dir.filePath("missing/file"), original, error), "missing parent rejected");
#if defined(__linux__)
        // A child-only file-size limit injects an actual filesystem write/flush
        // failure even for privileged test runners, without filling the disk.
        const auto child = fork();
        require(child >= 0, "fork");
        if (child == 0) {
            std::signal(SIGXFSZ, SIG_IGN);
            const rlimit limit{1, 1};
            if (setrlimit(RLIMIT_FSIZE, &limit) != 0) _exit(2);
            const bool saved = s7studio::writeFileAtomically(path, QByteArray(65536, 'x'), error);
            _exit(!saved && !error.isEmpty() ? 0 : 3);
        }
        int status = 0;
        require(waitpid(child, &status, 0) == child, "wait child");
        require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "write failure reported");
        require(read(path) == replacement, "failed write preserves existing destination");
#endif
        require(s7studio::writeFileAtomically(path, original, error), "save after failure");
        require(read(path) == original, "reopen after recovery");
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
