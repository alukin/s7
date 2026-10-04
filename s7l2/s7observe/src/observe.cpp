#include "s7/observe.hpp"

#include <thread>

namespace s7 {

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
    sample.element_id = element_id;
    sample.port = port;
    sample.type = runtime_->output_type_of(element_id, port);

    if (sample.is_complex()) {
        sample.complex_value = runtime_->complex_output_of(element_id, port);
    } else {
        sample.real_value = runtime_->real_output_of(element_id, port);
    }

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
    snapshot.samples.reserve(probes.size());
    for (const auto& probe : probes) {
        snapshot.samples.push_back(read(probe.element_id, probe.port));
    }
    return snapshot;
}

} // namespace s7
