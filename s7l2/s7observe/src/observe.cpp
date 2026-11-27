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

std::expected<CommittedArraySample, ArrayPoolError> ObservationService::try_retain_array_output(
    const ProbePoint& probe, const ArrayPool::Retention& retention) const
{
    return runtime_->try_retain_array_output(probe.element_id, probe.port, retention);
}

std::expected<CommittedArraySample, ArrayPoolError> ObservationService::try_retain_array_output(
    const ArrayProbePoint& probe) const
{
    require_stepping_thread();
    for (const auto& admission : probe.admissions) {
        auto sample = try_retain_array_output(probe.probe, admission);
        if (sample || sample.error() != ArrayPoolError::InvalidLease) return sample;
    }
    return std::unexpected(ArrayPoolError::InvalidLease);
}

std::expected<ArrayObservationSnapshot, ArrayPoolError> ObservationService::array_snapshot(
    const std::span<const ArrayProbePoint> probes) const
{
    require_stepping_thread();
    ArrayObservationSnapshot snapshot{runtime_->step_count(), {}};
    snapshot.samples.reserve(probes.size());
    for (const auto& probe : probes) {
        auto sample = try_retain_array_output(probe);
        if (!sample) return std::unexpected(sample.error());
        snapshot.samples.push_back(std::move(*sample));
    }
    return snapshot;
}

CompiledProbes ObservationService::compile(const std::vector<ProbePoint>& probes) const {
    require_stepping_thread();
    std::vector<CompiledProbes::Point> points;
    points.reserve(probes.size());
    for (const auto& probe : probes) {
        CompiledProbes::Point point;
        point.type = runtime_->output_type_of(probe.element_id, probe.port);
        point.value = runtime_->output_binding_of(probe.element_id, probe.port);
        // The same explicit conversion as ObservationSample::to_display_double,
        // chosen once per probe instead of dispatched on every read.
        point.display = visit_signal_type(point.type, []<SignalType T>() -> double (*)(const void*) {
            if constexpr (signal_type_info(T).is_complex()) {
                return nullptr;
            } else {
                return +[](const void* storage) -> double {
                    signal_storage_t<T> value{};
                    std::memcpy(&value, storage, sizeof(value));
                    return static_cast<double>(value);
                };
            }
        });
        points.push_back(point);
    }
    return CompiledProbes(*runtime_, std::move(points));
}

CompiledProbes::CompiledProbes(const NodeRuntime& runtime, std::vector<Point> points)
    : runtime_(&runtime), points_(std::move(points)) {
    for (const auto& point : points_) displayable_ = displayable_ && point.display != nullptr;
}

void CompiledProbes::require_stepping_thread() const {
    const auto stepping = runtime_->stepping_thread();
    if (stepping != std::thread::id{} && stepping != std::this_thread::get_id()) {
        throw std::logic_error("CompiledProbes must be read on the thread that steps the runtime.");
    }
}

void CompiledProbes::read_display(const std::span<double> out) const {
    if (!displayable_) throw std::invalid_argument("A complex probe has no single display value.");
    if (out.size() != points_.size()) throw std::invalid_argument("Display buffer size differs from the probe count.");
    require_stepping_thread();
    for (std::size_t index = 0; index < points_.size(); ++index) out[index] = points_[index].display(points_[index].value);
}

void CompiledProbes::read(const std::size_t index, void* destination) const {
    require_stepping_thread();
    const auto& point = points_.at(index);
    std::memcpy(destination, point.value, signal_type_info(point.type).storage_bytes);
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
