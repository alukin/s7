// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef S7TESTS_NUMERICAL_INTEGRATION_RESEARCH_HPP
#define S7TESTS_NUMERICAL_INTEGRATION_RESEARCH_HPP

#include <s7/core.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace s7tests::research {

// Research-only execution policy over validated, compiled S7 elements. It
// deliberately does not alter NodeRuntime, Element::step, or published APIs.
class CoupledPolicy {
public:
    enum class Method { EulerSubsteps, GaussLegendre2 };

    struct Counts {
        std::size_t accepted_steps{};
        std::size_t failed_steps{};
        std::size_t base_derivatives{};
        std::size_t trial_derivatives{};
        std::size_t pure_element_evaluations{};
        std::size_t stage_residual_evaluations{};
        std::size_t jacobian_probes{};
        std::size_t line_search_trials{};
        std::size_t solver_iterations{};
        double max_normalized_residual{};
        [[nodiscard]] std::size_t total_derivatives() const { return base_derivatives + trial_derivatives; }
    };

    CoupledPolicy(const s7::Schema& schema, s7::ExecutableGraph graph, double h,
                  Method method, int substeps = 1, int max_iterations = 12)
        : graph_(std::move(graph)), h_(h), method_(method), substeps_(substeps), max_iterations_(max_iterations)
    {
        if (!(std::isfinite(h_) && h_ > 0) || substeps_ < 1 || max_iterations_ < 0)
            throw std::invalid_argument("Invalid research integration configuration");
        if (!s7::Validator::validate(schema).ok())
            throw std::invalid_argument("Research graph failed S7 validation");
        if (graph_.memory_order.empty()) throw std::invalid_argument("Research graph has no integrator states");

        for (const auto& id : graph_.memory_order) {
            auto* element = graph_.elements.at(id).get();
            if (!dynamic_cast<s7::EulerIntegrator*>(element) || element->output_ports().size() != 1 ||
                element->output_ports()[0].type != s7::SignalType::F64 ||
                element->declared_rate() != s7::Rate::from_seconds(h_))
                throw std::invalid_argument("Research policy requires only F64 EulerIntegrator states at the outer dt: " + id);
            state_ids_.push_back(id);
            state_values_.push_back(static_cast<double*>(element->output_ports()[0].value_ptr));
        }

        std::unordered_map<std::string, std::vector<std::string>> parents;
        std::unordered_set<std::string> state_set(state_ids_.begin(), state_ids_.end());
        for (const auto& edge : schema.connections()) {
            parents[edge.to_element].push_back(edge.from_element);
        }
        for (const auto& id : graph_.causal_order) {
            auto* element = graph_.elements.at(id).get();
            const bool safe = dynamic_cast<s7::Constant*>(element) || dynamic_cast<s7::Gain*>(element) ||
                              dynamic_cast<s7::Adder*>(element) || dynamic_cast<s7::Multiply*>(element);
            if (!safe || element->has_state() || element->output_ports().size() != 1 ||
                element->output_ports()[0].type != s7::SignalType::F64)
                throw std::invalid_argument("Unsafe or unsupported research derivative element: " + id);
            base_order_.push_back(element);
            pure_values_.push_back(static_cast<double*>(element->output_ports()[0].value_ptr));
        }

        std::unordered_set<std::string> upstream;
        for (const auto& state_id : state_ids_) {
            const auto matches = std::ranges::count_if(schema.connections(), [&](const s7::Connection& edge) {
                return edge.to_element == state_id && edge.to_port == 0;
            });
            if (matches != 1) throw std::invalid_argument("Every research integrator requires one derivative input");
            for (const auto& edge : schema.connections()) {
                if (edge.to_element != state_id || edge.to_port != 0) continue;
                auto* source = graph_.elements.at(edge.from_element).get();
                if (edge.from_port >= source->output_ports().size() ||
                    source->output_ports()[edge.from_port].type != s7::SignalType::F64)
                    throw std::invalid_argument("Derivative input must be F64");
                derivative_values_.push_back(static_cast<const double*>(source->output_ports()[edge.from_port].value_ptr));
                upstream.insert(edge.from_element);
            }
        }
        // Reverse reachability from every derivative, stopping at memory.
        std::vector<std::string> queue(upstream.begin(), upstream.end());
        for (std::size_t i = 0; i < queue.size(); ++i) {
            if (state_set.contains(queue[i])) continue;
            for (const auto& parent : parents[queue[i]]) {
                if (upstream.insert(parent).second) queue.push_back(parent);
            }
        }
        // Forward reachability from selected integrator outputs in compiled
        // topological order, then intersect with derivative ancestors.
        std::unordered_set<std::string> downstream = state_set;
        for (const auto& id : graph_.causal_order) {
            bool varies = false;
            for (const auto& parent : parents[id]) varies = downstream.contains(parent) || varies;
            if (varies) downstream.insert(id);
            if (varies && upstream.contains(id)) trial_order_.push_back(graph_.elements.at(id).get());
        }
    }

    void step()
    {
        const auto previous_state = read_state();
        const auto previous_pure = read_pure();
        try {
            if (pending_) write_state(*pending_);
            for (auto* element : base_order_) {
                element->step();
                ++counts_.pure_element_evaluations;
            }
            const auto base = read_state();
            const auto f0 = read_derivative(false);
            const auto base_pure = read_pure();
            const auto candidate = method_ == Method::EulerSubsteps ? euler_substeps(base, f0)
                                                                    : gauss_legendre(base, f0);
            write_state(base);
            write_pure(base_pure);
            pending_ = candidate;
            ++counts_.accepted_steps;
        } catch (...) {
            write_state(previous_state);
            write_pure(previous_pure);
            ++counts_.failed_steps;
            throw;
        }
    }

    [[nodiscard]] std::vector<double> published_state() const { return read_state(); }
    [[nodiscard]] double published_of(const std::string& id) const {
        const auto it = std::find(state_ids_.begin(), state_ids_.end(), id);
        if (it == state_ids_.end()) throw std::out_of_range("Unknown research state: " + id);
        return *state_values_[static_cast<std::size_t>(it-state_ids_.begin())];
    }
    [[nodiscard]] const std::vector<double>& staged_state() const {
        if (!pending_) throw std::logic_error("No accepted stage yet");
        return *pending_;
    }
    [[nodiscard]] double staged_of(const std::string& id) const {
        const auto it = std::find(state_ids_.begin(), state_ids_.end(), id);
        if (it == state_ids_.end()) throw std::out_of_range("Unknown research state: " + id);
        return staged_state()[static_cast<std::size_t>(it-state_ids_.begin())];
    }
    void set_solver_limit(int limit) {
        if (limit < 0) throw std::invalid_argument("Negative research solver limit");
        max_iterations_ = limit;
    }
    [[nodiscard]] const Counts& counts() const { return counts_; }
    [[nodiscard]] std::size_t state_count() const { return state_values_.size(); }
    [[nodiscard]] std::size_t trial_element_count() const { return trial_order_.size(); }
    // Numerical payload estimate for stage/Jacobian/snapshot vectors; excludes
    // std::vector allocator metadata, capacity slack, and the compiled graph.
    [[nodiscard]] std::size_t stage_scratch_bytes_estimate() const {
        const auto n = state_count();
        const auto d = 2*n;
        return sizeof(double)*(d*d + 8*d + 4*n + pure_values_.size());
    }

private:
    [[nodiscard]] std::vector<double> read_state() const {
        std::vector<double> result;
        result.reserve(state_values_.size());
        for (const auto* value : state_values_) result.push_back(*value);
        return result;
    }
    void write_state(const std::vector<double>& values) {
        for (std::size_t i = 0; i < values.size(); ++i) *state_values_[i] = values[i];
    }
    [[nodiscard]] std::vector<double> read_pure() const {
        std::vector<double> result;
        result.reserve(pure_values_.size());
        for (const auto* value : pure_values_) result.push_back(*value);
        return result;
    }
    void write_pure(const std::vector<double>& values) {
        for (std::size_t i = 0; i < values.size(); ++i) *pure_values_[i] = values[i];
    }
    [[nodiscard]] std::vector<double> read_derivative(bool trial) {
        std::vector<double> result;
        result.reserve(derivative_values_.size());
        for (const auto* value : derivative_values_) {
            if (!std::isfinite(*value)) throw std::runtime_error("Nonfinite derivative in research stage");
            result.push_back(*value);
        }
        if (trial) ++counts_.trial_derivatives;
        else ++counts_.base_derivatives;
        return result;
    }
    [[nodiscard]] std::vector<double> evaluate_at(const std::vector<double>& state) {
        for (const double value : state) {
            if (!std::isfinite(value)) throw std::runtime_error("Nonfinite research trial state");
        }
        write_state(state);
        for (auto* element : trial_order_) {
            element->step();
            ++counts_.pure_element_evaluations;
        }
        return read_derivative(true);
    }
    [[nodiscard]] std::vector<double> euler_substeps(const std::vector<double>& base,
                                                      const std::vector<double>& f0) {
        std::vector<double> current = base;
        const double delta = h_/substeps_;
        for (int j = 0; j < substeps_; ++j) {
            const auto derivative = j == 0 ? f0 : evaluate_at(current);
            for (std::size_t i = 0; i < current.size(); ++i) current[i] += delta*derivative[i];
            for (double value : current) {
                if (!std::isfinite(value)) throw std::runtime_error("Nonfinite Euler substep");
            }
        }
        return current;
    }
    static double infinity_norm(const std::vector<double>& values) {
        double result = 0;
        for (double value : values) result = std::max(result, std::abs(value));
        return result;
    }
    [[nodiscard]] std::vector<double> stage_residual(const std::vector<double>& base,
                                                      const std::vector<double>& k) {
        const auto n = state_count();
        constexpr double a11 = .25;
        constexpr double a12 = .25 - 0.28867513459481288225;
        constexpr double a21 = .25 + 0.28867513459481288225;
        constexpr double a22 = .25;
        std::vector<double> stage1(n), stage2(n);
        for (std::size_t i = 0; i < n; ++i) {
            stage1[i] = base[i] + h_*(a11*k[i]+a12*k[n+i]);
            stage2[i] = base[i] + h_*(a21*k[i]+a22*k[n+i]);
        }
        const auto f1 = evaluate_at(stage1);
        const auto f2 = evaluate_at(stage2);
        std::vector<double> residual(2*n);
        for (std::size_t i = 0; i < n; ++i) {
            residual[i] = k[i]-f1[i];
            residual[n+i] = k[n+i]-f2[i];
        }
        ++counts_.stage_residual_evaluations;
        return residual;
    }
    static std::vector<double> solve(std::vector<std::vector<double>> matrix, std::vector<double> rhs) {
        const auto n = rhs.size();
        for (std::size_t col = 0; col < n; ++col) {
            std::size_t pivot = col;
            for (std::size_t row = col+1; row < n; ++row)
                if (std::abs(matrix[row][col]) > std::abs(matrix[pivot][col])) pivot = row;
            if (!(std::isfinite(matrix[pivot][col]) && std::abs(matrix[pivot][col]) > 1e-14))
                throw std::runtime_error("Singular Gauss-Legendre stage Jacobian");
            std::swap(matrix[pivot], matrix[col]);
            std::swap(rhs[pivot], rhs[col]);
            for (std::size_t row = col+1; row < n; ++row) {
                const double factor = matrix[row][col]/matrix[col][col];
                for (std::size_t j = col; j < n; ++j) matrix[row][j] -= factor*matrix[col][j];
                rhs[row] -= factor*rhs[col];
            }
        }
        std::vector<double> solution(n);
        for (std::size_t reverse = n; reverse > 0; --reverse) {
            const auto row = reverse-1;
            double value = rhs[row];
            for (std::size_t col = row+1; col < n; ++col) value -= matrix[row][col]*solution[col];
            solution[row] = value/matrix[row][row];
        }
        return solution;
    }
    [[nodiscard]] std::vector<double> gauss_legendre(const std::vector<double>& base,
                                                      const std::vector<double>& f0) {
        const auto n = state_count(), dimension = 2*n;
        std::vector<double> k(dimension);
        for (std::size_t i = 0; i < n; ++i) k[i] = k[n+i] = f0[i];
        auto residual = stage_residual(base, k);
        constexpr double atol = 1e-13, rtol = 1e-13;
        for (int iteration = 0; ; ++iteration) {
            const double normalized = infinity_norm(residual)/(atol + rtol*std::max(1., infinity_norm(k)));
            if (!std::isfinite(normalized)) throw std::runtime_error("Nonfinite Gauss-Legendre residual");
            if (normalized <= 1) {
                counts_.max_normalized_residual = std::max(counts_.max_normalized_residual, normalized);
                std::vector<double> endpoint(n);
                for (std::size_t i = 0; i < n; ++i) endpoint[i] = base[i] + h_*(k[i]+k[n+i])/2;
                for (double value : endpoint)
                    if (!std::isfinite(value)) throw std::runtime_error("Nonfinite Gauss-Legendre endpoint");
                return endpoint;
            }
            if (iteration >= max_iterations_) throw std::runtime_error("Gauss-Legendre stage solve did not converge");
            std::vector<std::vector<double>> jacobian(dimension, std::vector<double>(dimension));
            for (std::size_t col = 0; col < dimension; ++col) {
                auto perturbed = k;
                const double epsilon = std::sqrt(std::numeric_limits<double>::epsilon())*(1+std::abs(k[col]));
                perturbed[col] += epsilon;
                const auto displaced = stage_residual(base, perturbed);
                ++counts_.jacobian_probes;
                for (std::size_t row = 0; row < dimension; ++row)
                    jacobian[row][col] = (displaced[row]-residual[row])/epsilon;
            }
            auto negative = residual;
            for (auto& value : negative) value = -value;
            const auto delta = solve(jacobian, negative);
            bool accepted = false;
            for (int search = 0; search < 10; ++search) {
                const double factor = std::ldexp(1., -search);
                auto candidate = k;
                for (std::size_t i = 0; i < dimension; ++i) candidate[i] += factor*delta[i];
                auto candidate_residual = stage_residual(base, candidate);
                ++counts_.line_search_trials;
                if (infinity_norm(candidate_residual) < infinity_norm(residual)) {
                    k = std::move(candidate);
                    residual = std::move(candidate_residual);
                    accepted = true;
                    break;
                }
            }
            ++counts_.solver_iterations;
            if (!accepted) throw std::runtime_error("Gauss-Legendre line search did not reduce residual");
        }
    }

    s7::ExecutableGraph graph_;
    double h_;
    Method method_;
    int substeps_;
    int max_iterations_;
    std::vector<std::string> state_ids_;
    std::vector<double*> state_values_;
    std::vector<double*> pure_values_;
    std::vector<const double*> derivative_values_;
    std::vector<s7::Element*> base_order_;
    std::vector<s7::Element*> trial_order_;
    std::optional<std::vector<double>> pending_;
    Counts counts_;
};

} // namespace s7tests::research

#endif
