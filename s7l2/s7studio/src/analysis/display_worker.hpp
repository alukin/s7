// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "worker_pool.hpp"
#include <QString>
#include <QThreadPool>
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace s7studio::analysis {
// One executing request and one replaceable pending request per display. Jobs
// capture immutable values, never widgets; destruction does not wait on the UI.
template <class Result> class DisplayWorker {
  public:
    using Cancel = std::function<bool()>;
    using Job = std::function<Result(const Cancel &)>;
    struct Completion {
        std::shared_ptr<const Result> value;
        QString error;
    };
    DisplayWorker() : state_(std::make_shared<State>()) {}
    ~DisplayWorker() { cancel(); }
    DisplayWorker(const DisplayWorker &) = delete;
    DisplayWorker &operator=(const DisplayWorker &) = delete;
    void submit(Job job) {
        const auto state = state_;
        std::lock_guard lock(state->mutex);
        ++state->epoch;
        state->ready.reset();
        state->pending = std::move(job);
        if (state->running)
            return;
        state->running = true;
        launch(state);
    }

    void cancel() {
        std::lock_guard lock(state_->mutex);
        ++state_->epoch;
        state_->pending.reset();
        state_->ready.reset();
    }
    std::optional<Completion> take() {
        std::lock_guard lock(state_->mutex);
        return std::exchange(state_->ready, {});
    }
    bool busy() const {
        std::lock_guard lock(state_->mutex);
        return state_->running || state_->pending.has_value();
    }

  private:
    struct State {
        std::mutex mutex;
        std::atomic<std::uint64_t> epoch{0};
        bool running = false;
        std::optional<Job> pending;
        std::optional<Completion> ready;
    };
    static void launch(const std::shared_ptr<State> &state) {
        analysisPool().start(
            [state] {
                Job job;
                std::uint64_t epoch;
                {
                    std::lock_guard lock(state->mutex);
                    if (!state->pending) {
                        state->running = false;
                        return;
                    }
                    job = std::move(*state->pending);
                    state->pending.reset();
                    epoch = state->epoch;
                }
                Completion ready;
                const Cancel canceled = [state, epoch] { return state->epoch != epoch; };
                try {
                    if (!canceled())
                        ready.value = std::make_shared<const Result>(job(canceled));
                } catch (const std::exception &error) {
                    ready.error = QString::fromUtf8(error.what());
                }
                std::lock_guard lock(state->mutex);
                if (!canceled())
                    state->ready = std::move(ready);
                // Yield the pool between frames. Continuously changing displays
                // cannot monopolize its threads or outrank transform requests.
                if (state->pending)
                    launch(state);
                else
                    state->running = false;
            },
            -1);
    }
    std::shared_ptr<State> state_;
};
} // namespace s7studio::analysis
