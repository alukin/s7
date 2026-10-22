// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/spectrum_worker.hpp"
#include <QThreadPool>
#include <atomic>
#include <mutex>

namespace s7studio::analysis {
namespace {
QThreadPool& pool()
{
    // Initialize the FFTW planner lock before the pool, so it outlives all jobs
    // during static destruction. Individual engines own their plans and scratch.
    Fft::initialize();
    static QThreadPool threads;
    static const bool initialized = [] { threads.setMaxThreadCount(2); return true; }();
    (void)initialized;
    return threads;
}
}
struct SpectrumWorker::State {
    struct Request {
        std::shared_ptr<const observation::LiveFrame> frame;
        SpectrumSettings settings;
        std::uint64_t revision, epoch;
    };
    mutable std::mutex mutex;
    std::atomic<std::uint64_t> epoch{1};
    bool running = false;
    std::optional<Request> pending;
    std::optional<SpectrumResult> ready;
    std::uint64_t replaced = 0;
    std::uint64_t analyzedEpoch = 0; // Accessed only by the sole active runnable.
    SpectrumAnalyzer analyzer;
    std::shared_ptr<const observation::LiveFrame> analyzedFrame;
};
SpectrumWorker::SpectrumWorker() : state_(std::make_shared<State>()) { (void)pool(); }
SpectrumWorker::~SpectrumWorker() { cancel(); }
void SpectrumWorker::cancel()
{
    std::lock_guard lock(state_->mutex);
    ++state_->epoch; state_->pending.reset(); state_->ready.reset(); state_->replaced = 0;
}
void SpectrumWorker::submit(std::shared_ptr<const observation::LiveFrame> frame, SpectrumSettings settings, std::uint64_t revision)
{
    if (!frame) return;
    {
        std::lock_guard lock(state_->mutex);
        if (state_->pending) ++state_->replaced;
        state_->pending = State::Request{std::move(frame), std::move(settings), revision, state_->epoch.load()};
        if (state_->running) return;
        state_->running = true;
    }
    pool().start([state = state_] {
        for (;;) {
            std::optional<State::Request> request;
            {
                std::lock_guard lock(state->mutex);
                if (!state->pending) { state->running = false; return; }
                request = std::move(state->pending); state->pending.reset();
            }
            if (state->analyzedEpoch != request->epoch) {
                state->analyzer.reset(); state->analyzedFrame.reset(); state->analyzedEpoch = request->epoch;
            }
            SpectrumResult result;
            try {
                result = state->analyzer.analyze(*request->frame, request->settings,
                    [&] { return state->epoch.load() != request->epoch; });
            } catch (const std::exception& ex) {
                state->analyzer.reset();
                result.state = SpectrumResult::State::Unavailable;
                result.message = QStringLiteral("Spectrum error: %1").arg(QString::fromUtf8(ex.what()));
            } catch (...) {
                state->analyzer.reset(); result.message = QStringLiteral("Unknown spectrum error.");
            }
            result.runId = request->frame->runId; result.generation = request->frame->generation;
            if (!result.reused) state->analyzedFrame = request->frame;
            result.frame = state->analyzedFrame;
            result.revision = request->revision;
            std::lock_guard lock(state->mutex);
            if (state->epoch.load() == request->epoch && result.state != SpectrumResult::State::Cancelled)
                state->ready = std::move(result);
        }
    });
}
std::optional<SpectrumResult> SpectrumWorker::takeResult()
{
    std::lock_guard lock(state_->mutex);
    auto result = std::move(state_->ready); state_->ready.reset(); return result;
}
bool SpectrumWorker::busy() const { std::lock_guard lock(state_->mutex); return state_->running; }
std::uint64_t SpectrumWorker::replacedRequests() const { std::lock_guard lock(state_->mutex); return state_->replaced; }
}
