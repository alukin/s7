// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "network_task_runner.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <stdexcept>
#include <chrono>
#include <thread>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    s7studio::ui::NetworkTaskRunner runner;
    QEventLoop loop;
    bool completed = false;
    bool failed = false;
    bool timedOut = false;
    QTimer::singleShot(5000, &loop, [&] { timedOut = true; loop.quit(); });

    if (!runner.start([] {}, [&] { completed = true; loop.quit(); }, [&](const QString&) { failed = true; })) return 1;
    if (!runner.busy() || runner.start([] {}, {}, {})) return 2;
    loop.exec();
    if (timedOut || !completed || failed || runner.busy()) return 3;

    completed = false;
    QString error;
    if (!runner.start([] {
            throw std::runtime_error("request failed");
        }, [&] { completed = true; loop.quit(); }, [&](const QString& message) { error = message; loop.quit(); })) return 4;
    loop.exec();
    if (timedOut || completed || error != QStringLiteral("request failed") || runner.busy()) return 5;
    bool callbackAfterClose = false;
    {
        s7studio::ui::NetworkTaskRunner closing;
        if (!closing.start([] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); },
                           [&] { callbackAfterClose = true; }, {})) return 6;
    }
    QCoreApplication::processEvents();
    if (callbackAfterClose) return 7;
    return 0;
}
