// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/observe.hpp"

#include <cstring>
#include <thread>

namespace s7 {

double ObservationSample::to_display_double() const {
    if (is_complex()) {
        throw std::invalid_argument("A complex " + std::string(signal_type_name(type)) +
                                    " value has no single display value.");
    }
    return visit_signal_type(type, [this]<SignalType T>() -> double {
        if constexpr (signal_type_info(T).is_complex()) {
            return 0.0; // excluded above
        } else {
            return static_cast<double>(as<T>());
        }
    });
}

ObservationService::ObservationService(const NodeRuntime& runtime) : runtime_(&runtime) {}

void ObservationService::require_stepping_thread() const {
    const auto stepping = runtime_->stepping_thread();
    if (stepping != std::thread::id{} && stepping != std::this_thread::get_id()) {
        throw std::logic_error("ObservationService must be used on the thread that steps the runtime.");
    }
}

ObservationSample ObservationService::read(const std::string& element_id, const std::size_t port) const {
    ObservationSample sample;
    sample.step = runtime_->step_count();
    sample.model_time_ns = runtime_->model_time_ns();
    sample.model_time_seconds = runtime_->model_time_seconds();
    sample.element_id = element_id;
    sample.port = port;
    sample.type = runtime_->output_type_of(element_id, port);
    std::memcpy(sample.storage.data(), runtime_->output_binding_of(element_id, port),
                signal_type_info(sample.type).storage_bytes);
    return sample;
}

ObservationSample ObservationService::sample_output(const std::string& element_id, std::size_t port) const {
    require_stepping_thread();
    return read(element_id, port);
}

ObservationSnapshot ObservationService::snapshot(const std::vector<ProbePoint>& probes) const {
    require_stepping_thread();
    ObservationSnapshot snapshot;
    snapshot.step = runtime_->step_count();
    snapshot.model_time_ns = runtime_->model_time_ns();
    snapshot.model_time_seconds = runtime_->model_time_seconds();
    snapshot.samples.reserve(probes.size());
    for (const auto& probe : probes) {
        snapshot.samples.push_back(read(probe.element_id, probe.port));
    }
    return snapshot;
}

} // namespace s7
