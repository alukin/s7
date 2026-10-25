// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "observation/live_acquisition.hpp"
#include <array>

namespace s7studio::analysis {
struct TimeViewSettings {
    std::size_t buckets = 1024;
    double lower = -std::numeric_limits<double>::infinity();
    double upper = std::numeric_limits<double>::infinity();
    std::array<bool, observation::LiveAcquisition::maxChannels> visible{};
    std::array<double, observation::LiveAcquisition::maxChannels> gain{};
    std::array<double, observation::LiveAcquisition::maxChannels> offset{};
    bool prepareTime = false;
    bool autoY = false;
    bool fitX = false;
    double timeSpan = 0;
};
struct TimeViewResult {
    std::shared_ptr<const observation::LiveFrame> frame, rawFrame;
    std::array<QList<QPointF>, observation::LiveAcquisition::maxChannels> points;
    QString warning;
    std::uint64_t revision = 0;
    double snapshotMs = 0, prepareMs = 0;
    bool preparedTime = false;
    bool sourceSnapshot = false;
    bool hasXRange = false;
    double xMin = 0, xMax = 1;
    bool hasYRange = false;
    double yMin = 0, yMax = 1;
};

// Copies bounded acquisition snapshots and prepares reduced Time traces on the
// shared pool. Requests are latest-wins: each viewer has at most one pending
// request behind its active job, so a slow renderer cannot build an unbounded queue.
class TimeViewWorker {
public:
    TimeViewWorker();
    ~TimeViewWorker();
    TimeViewWorker(const TimeViewWorker&) = delete;
    TimeViewWorker& operator=(const TimeViewWorker&) = delete;
    void submitSource(std::shared_ptr<observation::LiveAcquisition> source, bool triggered,
                      TimeViewSettings settings, std::uint64_t revision);
    void submitFrame(std::shared_ptr<const observation::LiveFrame> frame,
                     TimeViewSettings settings, std::uint64_t revision);
    std::optional<TimeViewResult> takeResult();
    void cancel();
    bool busy() const;
    std::uint64_t replacedRequests() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
