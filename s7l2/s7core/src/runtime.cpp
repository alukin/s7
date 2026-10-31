// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/core.hpp"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <limits>

namespace s7 {

void NodeRuntime::TrialRegion::add_output(void* value_ptr, std::size_t bytes) {
    if (bytes > 32) throw std::invalid_argument("Numerical trial output exceeds snapshot storage.");
    outputs.push_back({.value_ptr=value_ptr, .bytes=bytes});
}

void NodeRuntime::TrialRegion::capture() {
    for (auto& output : outputs) std::memcpy(output.base.data(), output.value_ptr, output.bytes);
}

void NodeRuntime::TrialRegion::restore() noexcept {
    for (auto& output : outputs) std::memcpy(output.value_ptr, output.base.data(), output.bytes);
}

void NodeRuntime::TrialRegion::replay(std::size_t* evaluated) {
    for (auto* element : rhs_execution) {
        element->step();
        if (evaluated) ++*evaluated;
    }
}

void NodeRuntime::CoupledStage::prepare_storage() {
    const auto n = region.states.size(), d = 2*n;
    base.resize(n); f0.resize(n); current.resize(n); endpoint.resize(n);
    stage1.resize(n); stage2.resize(n); f1.resize(n); f2.resize(n);
    k.resize(d); residual.resize(d); displaced.resize(d); candidate.resize(d);
    candidate_residual.resize(d); jacobian.resize(d*d); rhs.resize(d);
    delta.resize(d); row_scale.resize(d);
    diagnostics.scratch_payload_bytes = sizeof(double)*(8*n + 8*d + d*d) +
        trial.outputs.size()*sizeof(TrialRegion::Output);
}

NodeRuntime::NodeRuntime(ExecutableGraph executable_graph) : graph_(std::move(executable_graph))
{
    if (graph_.domains.empty()) {
        // A graph built without rate domains (not by Compiler) is one domain.
        DomainState state;
        state.causal = graph_.causal_execution;
        state.memory = graph_.memory_execution;
        state.trapezoid_rhs = graph_.trapezoid_rhs_execution;
        domains_.push_back(std::move(state));
    } else {
        for (const auto& domain : graph_.domains) {
            DomainState state;
            state.rate = domain.rate;
            state.causal = domain.causal_execution;
            state.memory = domain.memory_execution;
            state.trapezoid_rhs = domain.trapezoid_rhs_execution;
            state.captures = domain.captures;
            domains_.push_back(std::move(state));
        }
    }
    for (auto& region : graph_.coupled_regions) {
        auto domain = std::ranges::find_if(domains_, [&](const DomainState& state) {
            return state.rate == region.rate;
        });
        if (domain == domains_.end()) throw std::logic_error("Compiled coupled region has no rate domain.");
        CoupledStage stage;
        stage.region = std::move(region);
        domain->coupled_stages.push_back(std::move(stage));
    }
    for (auto& domain : domains_) build_domain(domain);
}

void NodeRuntime::build_domain(DomainState& domain)
{
    domain.memory_output_ranges.reserve(domain.memory.size());
    for (auto* element : domain.memory) {
        const auto ports = element->output_ports();
        const auto begin = domain.memory_outputs.size();
        domain.memory_outputs.reserve(begin + ports.size());
        for (const auto& port : ports) {
            domain.memory_outputs.push_back(MemoryOutput{
                .value_ptr = port.value_ptr,
                .bytes = signal_type_info(port.type).storage_bytes,
            });
        }
        domain.memory_output_ranges.push_back(MemoryOutputRange{begin, domain.memory_outputs.size()});
        auto* integration = dynamic_cast<TrapezoidIntegration*>(element);
        const bool implicit = integration && integration->implicit_trapezoid_enabled();
        domain.implicit_memory.push_back(implicit);
        if (implicit) {
            domain.trapezoids.push_back({integration, domain.memory_output_ranges.back()});
            for (const auto& port : ports) {
                domain.trapezoid_trial.add_output(port.value_ptr, signal_type_info(port.type).storage_bytes);
            }
        }
    }

    for (auto* element : domain.causal) {
        for (const auto& port : element->output_ports()) {
            domain.causal_outputs.push_back(CausalOutput{
                .value_ptr = port.value_ptr,
                .bytes = signal_type_info(port.type).storage_bytes,
            });
        }
    }
    for (auto* element : domain.trapezoid_rhs) {
        for (const auto& port : element->output_ports()) {
            domain.trapezoid_trial.add_output(port.value_ptr, signal_type_info(port.type).storage_bytes);
        }
    }
    domain.trapezoid_trial.rhs_execution = domain.trapezoid_rhs;
    for (auto& stage : domain.coupled_stages) {
        stage.trial.rhs_execution = stage.region.rhs_execution;
        for (auto* value : stage.region.states) {
            const auto it = std::ranges::find_if(domain.memory_outputs, [value](const MemoryOutput& output) {
                return output.value_ptr == value;
            });
            if (it == domain.memory_outputs.end()) throw std::logic_error("Compiled numerical state has no memory output in its rate domain.");
            stage.memory_output_indices.push_back(static_cast<std::size_t>(it-domain.memory_outputs.begin()));
            stage.trial.add_output(value, sizeof(double));
            const auto memory_index = std::ranges::find_if(domain.memory_output_ranges, [index=stage.memory_output_indices.back()]
                (const MemoryOutputRange& range) { return range.begin <= index && index < range.end; });
            domain.implicit_memory[static_cast<std::size_t>(memory_index-domain.memory_output_ranges.begin())] = true;
        }
        for (auto* element : stage.region.rhs_execution)
            for (const auto& port : element->output_ports())
                stage.trial.add_output(port.value_ptr, signal_type_info(port.type).storage_bytes);
        stage.prepare_storage();
    }
}

void NodeRuntime::stage_trapezoids(DomainState& domain) {
    if (domain.trapezoids.empty()) return;
    domain.trapezoid_trial.capture();
    try {
        for (auto& stage : domain.trapezoids) stage.integration->prepare_trapezoid_step();
        for (auto& stage : domain.trapezoids) stage.integration->publish_trapezoid_candidate();
        constexpr int max_iterations = 64;
        for (int iteration = 0; iteration < max_iterations; ++iteration) {
            domain.trapezoid_trial.replay();
            bool converged = true;
            for (auto& stage : domain.trapezoids) {
                converged = stage.integration->prepare_trapezoid_correction() && converged;
            }
            for (auto& stage : domain.trapezoids) stage.integration->publish_trapezoid_candidate();
            if (converged) {
                for (const auto& stage : domain.trapezoids) {
                    for (auto i = stage.outputs.begin; i < stage.outputs.end; ++i) {
                        auto& output = domain.memory_outputs[i];
                        std::memcpy(output.staged[domain.staged_index ^ 1].data(), output.value_ptr, output.bytes);
                    }
                }
                domain.trapezoid_trial.restore();
                return;
            }
        }
        throw std::runtime_error("Implicit TrapezoidIntegrator did not converge after 64 iterations; "
                                 "reduce dt (fixed-point solver requires a contractive step).");
    } catch (...) {
        domain.trapezoid_trial.restore();
        throw;
    }
}

namespace {
double infinity_norm(const std::vector<double>& values) {
    double maximum = 0;
    for (const double value : values) maximum = std::max(maximum, std::abs(value));
    return maximum;
}

void require_finite(const std::vector<double>& values, const char* context) {
    for (const double value : values)
        if (!std::isfinite(value)) throw std::runtime_error(std::string("Nonfinite ") + context + " in coupled integration.");
}
}

void NodeRuntime::evaluate_derivative(CoupledStage& stage, const std::vector<double>& state,
                                      std::vector<double>& result) {
    require_finite(state, "trial state");
    for (std::size_t i = 0; i < state.size(); ++i) *stage.region.states[i] = state[i];
    stage.trial.replay(&stage.diagnostics.pure_element_evaluations);
    for (std::size_t i = 0; i < result.size(); ++i) result[i] = *stage.region.derivatives[i];
    require_finite(result, "trial derivative");
    ++stage.diagnostics.trial_derivative_vectors;
}

void NodeRuntime::stage_residual(CoupledStage& stage, const std::vector<double>& k,
                                 std::vector<double>& result) {
    constexpr double a11 = .25, a12 = .25-0.28867513459481288225;
    constexpr double a21 = .25+0.28867513459481288225, a22 = .25;
    const auto n = stage.region.states.size();
    const auto h = stage.region.step_seconds;
    for (std::size_t i = 0; i < n; ++i) {
        stage.stage1[i] = stage.base[i] + h*(a11*k[i]+a12*k[n+i]);
        stage.stage2[i] = stage.base[i] + h*(a21*k[i]+a22*k[n+i]);
    }
    evaluate_derivative(stage, stage.stage1, stage.f1);
    evaluate_derivative(stage, stage.stage2, stage.f2);
    for (std::size_t i = 0; i < n; ++i) {
        result[i] = k[i]-stage.f1[i];
        result[n+i] = k[n+i]-stage.f2[i];
    }
    require_finite(result, "stage residual");
    ++stage.diagnostics.stage_residual_evaluations;
}

void NodeRuntime::solve_gauss_legendre(CoupledStage& stage) {
    const auto n = stage.region.states.size(), d = 2*n;
    for (std::size_t i = 0; i < n; ++i) stage.k[i] = stage.k[n+i] = stage.f0[i];
    stage_residual(stage, stage.k, stage.residual);
    constexpr double atol = 1e-13, rtol = 1e-13;
    constexpr int max_iterations = 12;
    for (int iteration = 0;; ++iteration) {
        const double normalized = infinity_norm(stage.residual) /
            (atol + rtol*std::max(1., infinity_norm(stage.k)));
        if (!std::isfinite(normalized)) throw std::runtime_error("Nonfinite Gauss-Legendre normalized residual.");
        stage.diagnostics.last_normalized_residual = normalized;
        if (normalized <= 1) {
            for (std::size_t i = 0; i < n; ++i)
                stage.current[i] = stage.base[i] + stage.region.step_seconds*(stage.k[i]+stage.k[n+i])/2;
            require_finite(stage.current, "Gauss-Legendre endpoint");
            return;
        }
        if (iteration == max_iterations)
            throw std::runtime_error("Gauss-Legendre stage solve did not converge after 12 Newton iterations; residual=" +
                                     std::to_string(normalized) + ".");
        for (std::size_t col = 0; col < d; ++col) {
            stage.candidate = stage.k;
            const double epsilon = std::sqrt(std::numeric_limits<double>::epsilon())*(1+std::abs(stage.k[col]));
            stage.candidate[col] += epsilon;
            stage_residual(stage, stage.candidate, stage.displaced);
            ++stage.diagnostics.jacobian_probes;
            for (std::size_t row = 0; row < d; ++row)
                stage.jacobian[row*d+col] = (stage.displaced[row]-stage.residual[row])/epsilon;
        }
        require_finite(stage.jacobian, "Gauss-Legendre Jacobian");
        for (std::size_t row = 0; row < d; ++row) {
            stage.rhs[row] = -stage.residual[row];
            double scale = 0;
            for (std::size_t col = 0; col < d; ++col)
                scale = std::max(scale, std::abs(stage.jacobian[row*d+col]));
            stage.row_scale[row] = scale;
            if (!(scale > 0)) throw std::runtime_error("Singular Gauss-Legendre stage Jacobian.");
        }
        // Scaled partial pivoting: the threshold is relative to the row,
        // rather than a fixed absolute number tied to one state scale.
        for (std::size_t col = 0; col < d; ++col) {
            std::size_t pivot = col;
            for (std::size_t row = col+1; row < d; ++row)
                if (std::abs(stage.jacobian[row*d+col])/stage.row_scale[row] >
                    std::abs(stage.jacobian[pivot*d+col])/stage.row_scale[pivot]) pivot = row;
            if (std::abs(stage.jacobian[pivot*d+col]) <=
                64*std::numeric_limits<double>::epsilon()*stage.row_scale[pivot])
                throw std::runtime_error("Ill-conditioned Gauss-Legendre stage Jacobian.");
            if (pivot != col) {
                for (std::size_t j = 0; j < d; ++j)
                    std::swap(stage.jacobian[col*d+j], stage.jacobian[pivot*d+j]);
                std::swap(stage.rhs[col], stage.rhs[pivot]);
                std::swap(stage.row_scale[col], stage.row_scale[pivot]);
            }
            for (std::size_t row = col+1; row < d; ++row) {
                const double factor = stage.jacobian[row*d+col]/stage.jacobian[col*d+col];
                for (std::size_t j = col; j < d; ++j)
                    stage.jacobian[row*d+j] -= factor*stage.jacobian[col*d+j];
                stage.rhs[row] -= factor*stage.rhs[col];
            }
        }
        for (std::size_t reverse = d; reverse > 0; --reverse) {
            const auto row = reverse-1;
            double value = stage.rhs[row];
            for (std::size_t col = row+1; col < d; ++col) value -= stage.jacobian[row*d+col]*stage.delta[col];
            stage.delta[row] = value/stage.jacobian[row*d+row];
        }
        require_finite(stage.delta, "Gauss-Legendre Newton correction");
        bool reduced = false;
        for (int search = 0; search < 10; ++search) {
            const double factor = std::ldexp(1., -search);
            for (std::size_t i = 0; i < d; ++i) stage.candidate[i] = stage.k[i]+factor*stage.delta[i];
            stage_residual(stage, stage.candidate, stage.candidate_residual);
            ++stage.diagnostics.line_search_trials;
            if (infinity_norm(stage.candidate_residual) < infinity_norm(stage.residual)) {
                stage.k.swap(stage.candidate);
                stage.residual.swap(stage.candidate_residual);
                reduced = true;
                break;
            }
        }
        ++stage.diagnostics.solver_iterations;
        if (!reduced) throw std::runtime_error("Gauss-Legendre line search did not reduce the stage residual.");
    }
}

void NodeRuntime::stage_coupled(DomainState& domain) {
    const auto next = domain.staged_index ^ 1;
    for (auto& stage : domain.coupled_stages) {
        stage.trial.capture();
        try {
            const auto n = stage.region.states.size();
            for (std::size_t i = 0; i < n; ++i) {
                stage.base[i] = *stage.region.states[i];
                stage.f0[i] = *stage.region.derivatives[i];
            }
            require_finite(stage.base, "published state");
            require_finite(stage.f0, "base derivative");
            ++stage.diagnostics.base_derivative_vectors;
            if (stage.region.method == CoupledMethod::EulerSubsteps) {
                stage.current = stage.base;
                const double delta = stage.region.step_seconds/stage.region.substeps;
                for (int j = 0; j < stage.region.substeps; ++j) {
                    if (j > 0) evaluate_derivative(stage, stage.current, stage.f1);
                    const auto& f = j == 0 ? stage.f0 : stage.f1;
                    for (std::size_t i = 0; i < n; ++i) stage.endpoint[i] = stage.current[i]+delta*f[i];
                    require_finite(stage.endpoint, "Euler substep endpoint");
                    stage.current.swap(stage.endpoint);
                }
            } else {
                solve_gauss_legendre(stage);
            }
            stage.trial.restore();
            for (std::size_t i = 0; i < n; ++i) {
                auto& output = domain.memory_outputs[stage.memory_output_indices[i]];
                std::memcpy(output.staged[next].data(), &stage.current[i], sizeof(double));
            }
        } catch (...) {
            stage.trial.restore();
            ++stage.diagnostics.failed_steps;
            throw;
        }
    }
}

void NodeRuntime::roll_back_step(DomainState& domain, const bool published_memory) {
    // A direct evaluation, numerical trial or causal-memory step faulted:
    // restore every causal output, and the memory outputs published at the
    // start of this step, so the step is all-or-nothing instead of a mix of
    // this step's and the previous step's outputs. Memory elements stage into
    // the unused buffer, so the values staged by the previous step survive
    // and a later successful step() republishes them. This covers only
    // published outputs, not private element state (e.g. a filter's internal
    // delay line) already advanced before the fault.
    for (auto& output : domain.causal_outputs) {
        std::memcpy(output.value_ptr, output.snapshot.data(), output.bytes);
    }
    if (published_memory) {
        for (auto& output : domain.memory_outputs) {
            std::memcpy(output.value_ptr, output.before_publish.data(), output.bytes);
        }
        domain.memory_outputs_staged = true;
    }
}

void NodeRuntime::set_model_time_step_ns(const std::optional<std::int64_t> step_ns) {
    if (step_ns && *step_ns <= 0) {
        throw std::invalid_argument("Model time step must be positive.");
    }
    set_model_time_rate(step_ns ? std::optional{Rate::reduced(1'000'000'000, static_cast<std::uint64_t>(*step_ns))}
                                : std::nullopt);
}

void NodeRuntime::set_model_time_rate(std::optional<Rate> rate) {
    if (rate) {
        *rate = Rate::reduced(rate->numerator, rate->denominator);
        for (const auto& [id, element] : graph_.elements) {
            // Declared rates in other domains are checked below, with their domain.
            if (graph_.domains.size() > 1 && graph_.rates.contains(id)) continue;
            if (const auto declared = element->declared_rate(); declared && *declared != *rate)
                throw std::invalid_argument("Model-time rate " + rate->to_string() + " conflicts with element '"
                    + id + "' at " + declared->to_string() + ". Change the run dt to match, or add a "
                    "Resampler where the rates meet.");
        }
        // Seeded and inferred rates count as much as declared ones: an element
        // validated at one rate must not be stepped as another, unless its rate
        // domain is joined through Resamplers to a domain at the model rate.
        std::vector<bool> joined(graph_.domains.size(), false);
        for (std::size_t i = 0; i < graph_.domains.size(); ++i) joined[i] = graph_.domains[i].rate == *rate;
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto& [a, b] : graph_.domain_links) {
                if (joined[a] != joined[b]) { joined[a] = joined[b] = true; changed = true; }
            }
        }
        for (const auto& [id, resolved] : graph_.rates) {
            const auto domain = std::ranges::find_if(graph_.domains, [&](const RateDomain& d) { return d.rate == resolved; });
            if (resolved != *rate && (domain == graph_.domains.end() || !joined[static_cast<std::size_t>(domain - graph_.domains.begin())]))
                throw std::invalid_argument("Model-time rate " + rate->to_string() + " conflicts with element '"
                    + id + "', which runs at " + resolved.to_string() + ". Change the run dt to match, or add a "
                    "Resampler where the rates meet.");
        }
    }
    model_time_rate_ = rate;
}

std::optional<Rate> NodeRuntime::graph_rate() const {
    std::optional<Rate> common;
    for (const auto& [id, rate] : graph_.rates) {
        if (common && *common != rate) return std::nullopt;
        common = rate;
    }
    return common;
}

namespace {
std::int64_t timestamp_ns(std::size_t step, Rate rate) {
    return rate.step_time_ns(step);
}
}
std::optional<std::int64_t> NodeRuntime::model_time_ns() const {
    if (!model_time_rate_) return std::nullopt;
    return timestamp_ns(step_count_, *model_time_rate_);
}
std::optional<double> NodeRuntime::model_time_seconds() const {
    if (!model_time_rate_) return std::nullopt;
    return static_cast<double>(static_cast<long double>(step_count_) * model_time_rate_->denominator
                               / model_time_rate_->numerator);
}

std::thread::id NodeRuntime::stepping_thread() const {
    return stepping_thread_.load();
}

void NodeRuntime::step() {
    if (step_count_ == std::numeric_limits<std::size_t>::max())
        throw std::overflow_error("Runtime step count exhausted.");
    if (model_time_rate_) static_cast<void>(timestamp_ns(step_count_ + 1, *model_time_rate_));
    stepping_thread_.store(std::this_thread::get_id());
    if (domains_.size() == 1 || (!model_time_rate_ && graph_.domain_links.empty())) {
        // One rate, or unrelated parts with no time base: one step each.
        for (auto& domain : domains_) step_domain(domain);
    } else {
        if (!model_time_rate_) {
            throw std::logic_error("This graph has several rate domains joined by Resamplers; set the model-time "
                                   "rate (the run's dt) to schedule them.");
        }
        // Each domain takes every sample at or before this step's model time,
        // input domains first. A fault leaves domains already stepped here
        // advanced; retrying continues exactly, since each counts its own samples.
        for (auto& domain : domains_) {
            const auto target = resampling::samples_through(step_count_, domain.rate, *model_time_rate_);
            while (domain.steps < target) {
                step_domain(domain);
                for (auto* resampler : domain.captures) resampler->capture_input();
            }
        }
    }
    ++step_count_;
}

void NodeRuntime::step_domain(DomainState& domain) {
    const bool publishing = domain.memory_outputs_staged;
    if (publishing) {
        for (auto& output : domain.memory_outputs) {
            std::memcpy(output.before_publish.data(), output.value_ptr, output.bytes);
            std::memcpy(output.value_ptr, output.staged[domain.staged_index].data(), output.bytes);
        }
        domain.memory_outputs_staged = false;
    }

    for (auto& output : domain.causal_outputs) {
        std::memcpy(output.snapshot.data(), output.value_ptr, output.bytes);
    }
    try {
        for (auto* element : domain.causal) element->step();
        stage_coupled(domain);
        stage_trapezoids(domain);
    } catch (...) {
        roll_back_step(domain, publishing);
        throw;
    }

    // Each causal-memory element computes its next output from this step's
    // inputs into the unused staging buffer; its published output is restored
    // right away so that other causal-memory elements still read this step's
    // values.
    const auto next = domain.staged_index ^ 1;
    for (std::size_t element_index = 0; element_index < domain.memory.size(); ++element_index) {
        if (domain.implicit_memory[element_index]) continue;
        const auto range = domain.memory_output_ranges[element_index];
        for (std::size_t output_index = range.begin; output_index < range.end; ++output_index) {
            auto& output = domain.memory_outputs[output_index];
            std::memcpy(output.snapshot.data(), output.value_ptr, output.bytes);
        }

        try {
            domain.memory[element_index]->step();
        } catch (...) {
            for (std::size_t output_index = range.begin; output_index < range.end; ++output_index) {
                auto& output = domain.memory_outputs[output_index];
                std::memcpy(output.value_ptr, output.snapshot.data(), output.bytes);
            }
            roll_back_step(domain, publishing);
            throw;
        }

        for (std::size_t output_index = range.begin; output_index < range.end; ++output_index) {
            auto& output = domain.memory_outputs[output_index];
            std::memcpy(output.staged[next].data(), output.value_ptr, output.bytes);
            std::memcpy(output.value_ptr, output.snapshot.data(), output.bytes);
        }
    }
    if (!domain.memory_outputs.empty()) {
        domain.staged_index = next;
        domain.memory_outputs_staged = true;
    }
    ++domain.steps;
}

const std::string& NodeRuntime::runtime_instance_uuid() const {
    return graph_.runtime_instance_uuid;
}

const std::string& NodeRuntime::schema_instance_uuid() const {
    return graph_.schema_instance_uuid;
}

const std::string& NodeRuntime::element_instance_uuid_of(const std::string& element_id) const {
    const auto it = graph_.id_to_instance_uuid.find(element_id);
    if (it == graph_.id_to_instance_uuid.end()) {
        throw std::out_of_range("Unknown element id requested: '" + element_id + "'.");
    }
    return it->second;
}

const std::string& NodeRuntime::element_id_of_instance_uuid(const std::string& element_instance_uuid) const {
    const auto it = graph_.instance_uuid_to_id.find(element_instance_uuid);
    if (it == graph_.instance_uuid_to_id.end()) {
        throw std::out_of_range("Unknown element instance UUID requested: '" + element_instance_uuid + "'.");
    }
    return it->second;
}

OutputBinding NodeRuntime::output_binding_of(const std::string& element_id, std::size_t port) const {
    const auto element_it = graph_.elements.find(element_id);
    if (element_it == graph_.elements.end()) {
        throw std::out_of_range("Unknown element output requested: '" + element_id + "'.");
    }

    const auto outputs = element_it->second->outputs();
    if (port >= outputs.size()) {
        throw std::out_of_range("Output port out of range for element '" + element_id + "'.");
    }

    return outputs[port];
}

SignalType NodeRuntime::output_type_of(const std::string& element_id, std::size_t port) const {
    const auto element_it = graph_.elements.find(element_id);
    if (element_it == graph_.elements.end()) {
        throw std::out_of_range("Unknown element output requested: '" + element_id + "'.");
    }

    const auto ports = element_it->second->output_ports();
    if (port >= ports.size()) {
        throw std::out_of_range("Output port out of range for element '" + element_id + "'.");
    }

    return ports[port].type;
}

SignalType NodeRuntime::output_type_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port) const {
    return output_type_of(element_id_of_instance_uuid(element_instance_uuid), port);
}

void NodeRuntime::require_output_type(const std::string& element_id, const std::size_t port, const SignalType expected) const {
    const auto actual = output_type_of(element_id, port);
    if (actual != expected) {
        throw std::invalid_argument("Output " + std::to_string(port) + " of '" + element_id + "' has type " +
                                    std::string(signal_type_name(actual)) + ", not " +
                                    std::string(signal_type_name(expected)) + ".");
    }
}

RealValue NodeRuntime::real_output_of(const std::string& element_id, std::size_t port) const {
    return output_of<SignalType::F64>(element_id, port);
}

RealValue NodeRuntime::real_output_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port) const {
    return real_output_of(element_id_of_instance_uuid(element_instance_uuid), port);
}

ComplexValue NodeRuntime::complex_output_of(const std::string& element_id, std::size_t port) const {
    return output_of<SignalType::C64>(element_id, port);
}

ComplexValue NodeRuntime::complex_output_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port) const {
    return complex_output_of(element_id_of_instance_uuid(element_instance_uuid), port);
}

std::size_t NodeRuntime::step_count() const {
    return step_count_;
}

NodeRuntime::NumericalDiagnostics NodeRuntime::numerical_diagnostics() const {
    NumericalDiagnostics total;
    for (const auto& domain : domains_) {
        if (!domain.coupled_stages.empty()) total.accepted_steps += domain.steps;
        for (const auto& stage : domain.coupled_stages) {
            const auto& value = stage.diagnostics;
            total.failed_steps += value.failed_steps;
            total.base_derivative_vectors += value.base_derivative_vectors;
            total.trial_derivative_vectors += value.trial_derivative_vectors;
            total.pure_element_evaluations += value.pure_element_evaluations;
            total.stage_residual_evaluations += value.stage_residual_evaluations;
            total.jacobian_probes += value.jacobian_probes;
            total.line_search_trials += value.line_search_trials;
            total.solver_iterations += value.solver_iterations;
            total.last_normalized_residual = std::max(total.last_normalized_residual, value.last_normalized_residual);
            total.scratch_payload_bytes += value.scratch_payload_bytes;
        }
    }
    // Count domain samples once per domain, even when it solves several regions.
    return total;
}

} // namespace s7
