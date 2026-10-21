// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once
#include "local_schema_runner.hpp"
#include "model_run_controller.hpp"
#include <s7/builtin_element_registry.hpp>
#include <QtTest>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <limits>

namespace {
struct Gate {
    std::atomic<int> entered{0};
    std::atomic<int> inStep{0};
    std::atomic<bool> destroyedInStep{false};
    std::atomic<bool> fail{false};
    std::atomic<bool> unknownFailure{false};
    std::mutex mutex;
    std::condition_variable changed;
    int permits = 0;
    bool open = false;
    void allow(int count = 1) { std::lock_guard lock(mutex); permits += count; changed.notify_all(); }
    void releaseAll() { std::lock_guard lock(mutex); open = true; changed.notify_all(); }
};
std::shared_ptr<Gate> currentGate;
class GatedElement : public s7::Element {
    std::shared_ptr<Gate> gate_;
    double value_;
    std::atomic<bool> stepping_{false};
public:
    GatedElement(std::shared_ptr<Gate> gate, double value) : Element("LifecycleGate", true), gate_(std::move(gate)), value_(value) {
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    ~GatedElement() override { if (stepping_.load()) gate_->destroyedInStep.store(true); }
    void step() override {
        stepping_.store(true);
        ++gate_->inStep;
        ++gate_->entered;
        {
            std::unique_lock lock(gate_->mutex);
            gate_->changed.wait(lock, [&] { return gate_->open || gate_->permits > 0; });
            if (gate_->permits > 0) --gate_->permits;
        }
        --gate_->inStep;
        stepping_.store(false);
        if (gate_->unknownFailure.load()) throw 42;
        if (gate_->fail.load()) throw std::runtime_error("injected worker step failure");
    }
};
auto schema(double value = 1) -> boost::json::value {
    return boost::json::object{{"name", "lifecycle"}, {"dt", .001},
        {"elements", boost::json::array{boost::json::object{{"id", "source"}, {"type", "LifecycleGate"}, {"params", {{"value", value}}}}}},
        {"connections", boost::json::array{}}};
}
}


inline void registerLifecycleElements() {
        s7::register_builtin_elements(s7::ElementRegistry::instance());
        s7::ElementMetadata metadata;
        metadata.type_name = "LifecycleGate";
        metadata.output_ports = {{"out", "test output", s7::SignalType::F64}};
        metadata.parameters = {{"value", "real", "test value", 1.0, {}, {}, {}, false}};
        s7::ElementRegistry::instance().register_element(s7::ElementDescriptor("LifecycleGate",
            [](const s7::ElementCreationContext& context) { return std::make_unique<GatedElement>(currentGate, context.get_real("value")); }, metadata));
    }
