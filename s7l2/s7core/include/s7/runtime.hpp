// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_RUNTIME_HPP
#define S7CORE_INCLUDE_S7_RUNTIME_HPP

#include "s7/graph.hpp"
#include "s7/resampler.hpp"
#include "s7/rate.hpp"
#include "s7/trapezoid.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace s7 {

// Not thread-safe: step() and every read of outputs or step_count() must run on
// one thread at a time (normally the thread that steps the runtime).
class NodeRuntime {
public:
    struct NumericalDiagnostics {
        std::size_t accepted_steps{};
        std::size_t failed_steps{};
        std::size_t base_derivative_vectors{};
        std::size_t trial_derivative_vectors{};
        std::size_t pure_element_evaluations{};
        std::size_t stage_residual_evaluations{};
        std::size_t jacobian_probes{};
        std::size_t line_search_trials{};
        std::size_t solver_iterations{};
        double max_last_normalized_residual{};
        std::size_t scratch_payload_bytes{};
    };

    explicit NodeRuntime(ExecutableGraph executable_graph);

    void step();
    // The thread that last called step() (default-constructed before the first step).
    [[nodiscard]] std::thread::id stepping_thread() const;

    // Model time advanced by one step, in nanoseconds. Unset means the graph
    // has no model-time base (steps are only ordered, not timed).
    void set_model_time_step_ns(std::optional<std::int64_t> step_ns);
    // Local rational model-time base. Every element's resolved rate (declared,
    // seeded or inferred) must equal it; a conflict names the element and
    // suggests a Resampler. Does not change scheduling or element dt.
    void set_model_time_rate(std::optional<Rate> rate);
    // The one rate every element of the graph resolved to, so callers can set
    // model time from the graph; nullopt for an empty graph or mixed rates.
    [[nodiscard]] std::optional<Rate> graph_rate() const;
    // Absolute step/rate, rounded only once to ns; throws on int64 overflow.
    [[nodiscard]] std::optional<std::int64_t> model_time_ns() const;
    [[nodiscard]] std::optional<double> model_time_seconds() const;
    [[nodiscard]] const std::string& runtime_instance_uuid() const;
    [[nodiscard]] const std::string& schema_instance_uuid() const;
    [[nodiscard]] const std::string& element_instance_uuid_of(const std::string& element_id) const;
    [[nodiscard]] const std::string& element_id_of_instance_uuid(const std::string& element_instance_uuid) const;
    [[nodiscard]] SignalType output_type_of(const std::string& element_id, std::size_t port = 0) const;
    [[nodiscard]] SignalType output_type_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port = 0) const;
    // Typed output read; throws std::invalid_argument if the port has another type.
    template <SignalType Type>
    [[nodiscard]] signal_storage_t<Type> output_of(const std::string& element_id, std::size_t port = 0) const
    {
        require_output_type(element_id, port, Type);
        return *static_cast<const signal_storage_t<Type>*>(output_binding_of(element_id, port));
    }
    // Address of an output's storage, valid between steps on the stepping thread.
    [[nodiscard]] OutputBinding output_binding_of(const std::string& element_id, std::size_t port) const;
    // F64 / C64 shorthands for output_of<F64> / output_of<C64>.
    [[nodiscard]] RealValue real_output_of(const std::string& element_id, std::size_t port = 0) const;
    [[nodiscard]] RealValue real_output_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port = 0) const;
    [[nodiscard]] ComplexValue complex_output_of(const std::string& element_id, std::size_t port = 0) const;
    [[nodiscard]] ComplexValue complex_output_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port = 0) const;
    [[nodiscard]] std::size_t step_count() const;
    // Aggregated counters for opt-in F64 coupled regions; existing trapezoid
    // and ordinary elements are deliberately not assigned fabricated counts.
    [[nodiscard]] NumericalDiagnostics numerical_diagnostics() const;

private:
    // The element output behind an output of a flattened nested schema.
    [[nodiscard]] std::pair<std::string, std::size_t> aliased_output(const std::string& element_id, std::size_t port) const;

    // Output of a causal-memory element. Its step() writes the next step's
    // value; the runtime stages it and publishes it at the start of the next
    // step, so all outputs read after step() belong to the same step.
    // Raw copies of the output's storage (at most 32 bytes: CI128/C128).
    // staged[staged_index_] holds the values to publish next; a step stages
    // into the other buffer and switches only after every memory element and
    // coupled trial succeeded, so a failed step keeps the earlier values.
    struct MemoryOutput {
        void* value_ptr{};
        std::size_t bytes{};
        std::array<std::byte, 32> snapshot{};
        std::array<std::byte, 32> before_publish{};
        std::array<std::array<std::byte, 32>, 2> staged{};
    };

    struct MemoryOutputRange {
        std::size_t begin{};
        std::size_t end{};
    };

    // Output of a causal-order element, snapshotted before causal_order runs
    // so a throwing element leaves no torn step: either every causal output
    // reflects this step, or (on exception) none of them do.
    struct CausalOutput {
        void* value_ptr{};
        std::size_t bytes{};
        std::array<std::byte, 32> snapshot{};
    };

    struct TrapezoidStage {
        TrapezoidIntegration* integration{};
        MemoryOutputRange outputs;
    };
    // Temporary trial storage, restored before any ordinary memory step or
    // observation. Solver failure restores it before propagating the error.
    struct TrialRegion {
        struct Output {
            void* value_ptr{};
            std::size_t bytes{};
            std::array<std::byte, 32> base{};
        };
        std::vector<Output> outputs;
        std::vector<Element*> rhs_execution;
        void add_output(void* value_ptr, std::size_t bytes);
        void capture();
        void restore() noexcept;
        void replay(std::size_t* evaluated = nullptr);
    };
    struct CoupledStage {
        struct StageInput {
            double* value{};
            std::size_t source_stage{};
            std::size_t source_state{};
            double first{};
            double second{};
        };
        ExecutableGraph::CoupledRegion region;
        TrialRegion trial;
        std::vector<std::size_t> memory_output_indices;
        std::vector<StageInput> stage_inputs;
        NumericalDiagnostics diagnostics;
        // Reused numerical buffers; no per-step stage or Jacobian allocation.
        std::vector<double> base, f0, current, endpoint, stage1, stage2, f1, f2;
        std::vector<double> k, residual, displaced, candidate, candidate_residual, jacobian, rhs, delta, row_scale;
        void prepare_storage();
    };
    // Everything one rate domain needs to step (doc/S7_Sample_Rate_Model.md
    // section 8.3). A single-rate graph has one; its step is the whole step.
    struct DomainState {
        Rate rate{};
        std::vector<Element*> causal;
        std::vector<Element*> memory;
        std::vector<Element*> trapezoid_rhs;
        std::vector<RateChangingElement*> captures;
        std::vector<MemoryOutput> memory_outputs;
        std::vector<MemoryOutputRange> memory_output_ranges;
        std::vector<CausalOutput> causal_outputs;
        std::vector<TrapezoidStage> trapezoids;
        TrialRegion trapezoid_trial;
        std::vector<CoupledStage> coupled_stages;
        std::vector<bool> implicit_memory;
        bool memory_outputs_staged{};
        std::size_t staged_index{};
        std::uint64_t steps{}; // samples this domain has completed
    };
    void build_domain(DomainState& domain);
    void step_domain(DomainState& domain);
    static void stage_trapezoids(DomainState& domain);
    void stage_coupled(DomainState& domain);
    void evaluate_derivative(CoupledStage& stage, const std::vector<double>& state, std::vector<double>& result);
    void stage_residual(CoupledStage& stage, const std::vector<double>& k, std::vector<double>& result);
    void solve_gauss_legendre(CoupledStage& stage);
    // Restores every published output of the domain to its pre-step value.
    static void roll_back_step(DomainState& domain, bool published_memory);

    void require_output_type(const std::string& element_id, std::size_t port, SignalType expected) const;

    ExecutableGraph graph_;
    std::vector<DomainState> domains_;
    std::atomic<std::thread::id> stepping_thread_{};
    std::optional<Rate> model_time_rate_;
    std::size_t step_count_{};
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_RUNTIME_HPP
