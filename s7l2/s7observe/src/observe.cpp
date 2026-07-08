#include "s7/observe.hpp"

namespace s7 {

ObservationService::ObservationService(const NodeRuntime& runtime) : runtime_(&runtime) {}

ObservationSample ObservationService::sample_output(const std::string& element_id, std::size_t port) const {
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

std::vector<ObservationSample> ObservationService::sample_outputs(const std::vector<ProbePoint>& probes) const {
    std::vector<ObservationSample> samples;
    samples.reserve(probes.size());

    for (const auto& probe : probes) {
        samples.push_back(sample_output(probe.element_id, probe.port));
    }

    return samples;
}

} // namespace s7
