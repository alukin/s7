// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/pair_analysis.hpp"
#include "analysis/worker_pool.hpp"
#include <QThreadPool>
#include <atomic>
#include <mutex>
namespace s7studio::analysis {
struct PairWorker::State {
    struct Request { std::shared_ptr<const observation::LiveFrame> frame; PairSettings settings; std::uint64_t revision, epoch; };
    explicit State(PairKind value) : kind(value) {}
    const PairKind kind;
    mutable std::mutex mutex;
    std::atomic<std::uint64_t> epoch{1};
    std::optional<Request> pending;
    std::optional<PairResult> ready;
    bool running = false;
    std::uint64_t replaced = 0, analyzedEpoch = 0;
    PairAnalyzer analyzer;
};
PairWorker::PairWorker(PairKind kind) : state_(std::make_shared<State>(kind)) { (void)analysisPool(); }
PairWorker::~PairWorker() { cancel(); }
void PairWorker::cancel() { std::lock_guard lock(state_->mutex); ++state_->epoch; state_->pending.reset(); state_->ready.reset(); state_->replaced = 0; }
bool PairWorker::busy() const { std::lock_guard lock(state_->mutex); return state_->running; }
std::uint64_t PairWorker::replacedRequests() const { std::lock_guard lock(state_->mutex); return state_->replaced; }
std::optional<PairResult> PairWorker::takeResult() {
    std::lock_guard lock(state_->mutex); auto result = std::move(state_->ready); state_->ready.reset(); return result;
}
void PairWorker::submit(std::shared_ptr<const observation::LiveFrame> frame, PairSettings settings, std::uint64_t revision)
{
    if (!frame) return;
    {
        std::lock_guard lock(state_->mutex); if (state_->pending) ++state_->replaced;
        state_->pending = State::Request{std::move(frame), settings, revision, state_->epoch.load()};
        if (state_->running) return;
        state_->running = true;
    }
    analysisPool().start([state = state_] {
        for (;;) {
            std::optional<State::Request> request;
            {
                std::lock_guard lock(state->mutex);
                if (!state->pending) { state->running = false; return; }
                request = std::move(state->pending); state->pending.reset();
            }
            if (state->analyzedEpoch != request->epoch) { state->analyzer.reset(); state->analyzedEpoch = request->epoch; }
            PairResult result;
            try { result = state->analyzer.analyze(state->kind, *request->frame, request->settings, [&] { return state->epoch.load() != request->epoch; }); }
            catch (const std::exception& ex) { state->analyzer.reset(); result.message = QStringLiteral("Analysis error: %1").arg(QString::fromUtf8(ex.what())); }
            catch (...) { state->analyzer.reset(); result.message = "Unknown pair analysis error."; }
            result.kind = state->kind; result.runId = request->frame->runId; result.generation = request->frame->generation;
            result.frame = request->frame; result.revision = request->revision;
            std::lock_guard lock(state->mutex);
            if (state->epoch.load() == request->epoch && result.state != PairResult::State::Cancelled) state->ready = std::move(result);
        }
    });
}
}
