// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "analysis/spectrum.hpp"
#include <memory>

namespace s7studio::analysis {
// One active request and one replaceable pending request per panel; all panels
// share two analysis threads. Completion is polled, never delivered to a dead UI.
class SpectrumWorker {
public:
    SpectrumWorker();
    ~SpectrumWorker();
    SpectrumWorker(const SpectrumWorker&) = delete;
    SpectrumWorker& operator=(const SpectrumWorker&) = delete;
    void submit(std::shared_ptr<const observation::LiveFrame> frame, SpectrumSettings settings, std::uint64_t revision);
    std::optional<SpectrumResult> takeResult();
    void cancel();
    bool busy() const;
    std::uint64_t replacedRequests() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
