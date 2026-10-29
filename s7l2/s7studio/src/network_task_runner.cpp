// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "network_task_runner.hpp"

#include <QMetaObject>

#include <exception>
#include <utility>

namespace s7studio::ui {

NetworkTaskRunner::~NetworkTaskRunner()
{
    requestCancel();
    if (worker_.joinable())
        worker_.join();
}

bool NetworkTaskRunner::start(Work work, Completion finished, std::function<void(const QString&)> failed)
{
    if (busy_)
        return false;
    if (worker_.joinable())
        worker_.join();
    cancelled_.store(false);
    busy_ = true;
    try {
        worker_ = std::thread([this, work = std::move(work), finished = std::move(finished), failed = std::move(failed)] {
            QString error;
            try {
                work(cancelled_);
            } catch (const std::exception& ex) {
                error = QString::fromUtf8(ex.what());
            } catch (...) {
                error = QStringLiteral("Unknown network request error");
            }
            QMetaObject::invokeMethod(
                this,
                [this, finished, failed, error] {
                    if (worker_.joinable())
                        worker_.join();
                    busy_ = false;
                    try {
                        if (error.isEmpty()) {
                            if (finished)
                                finished();
                        } else if (failed) {
                            failed(error);
                        }
                    } catch (const std::exception& ex) {
                        if (failed)
                            failed(QString::fromUtf8(ex.what()));
                    } catch (...) {
                        if (failed)
                            failed(QStringLiteral("Unknown network completion error"));
                    }
                },
                Qt::QueuedConnection);
        });
    } catch (...) {
        busy_ = false;
        throw;
    }
    return true;
}

} // namespace s7studio::ui
