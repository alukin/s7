// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <thread>

namespace s7studio::ui {

// Runs one blocking network request at a time. Both callbacks execute on the
// owner thread; destruction joins the worker before the QObject is destroyed.
class NetworkTaskRunner final : public QObject
{
public:
    using Completion = std::function<void()>;
    using Work = std::function<void()>;

    explicit NetworkTaskRunner(QObject* parent = nullptr) : QObject(parent) {}
    ~NetworkTaskRunner() override;

    // Owner thread only. Returns false while another request is pending.
    bool start(Work work, Completion finished, std::function<void(const QString&)> failed);
    [[nodiscard]] bool busy() const { return busy_; }

private:
    std::thread worker_;
    bool busy_ = false;
};

} // namespace s7studio::ui
