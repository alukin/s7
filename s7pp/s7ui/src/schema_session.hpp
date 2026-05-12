//
// schema_session.hpp — owns a live s7::Schema and manages the connection
// between the computation engine and the GUI elements.
//
// Responsibilities:
//   - Build a Schema from a netlist JSON (which includes Probe nodes for
//     every visual-sink item in the scene).
//   - Run the Schema asynchronously in a background thread.
//   - Support pause / resume / stop with atomic flags checked each tick.
//   - Expose per-sink Probe shared_ptrs so GUI widgets can read sample data.
//
// Threading model:
//   - The background thread runs schema ticks and writes Probe ring buffers.
//   - The GUI thread reads Probe buffers via QTimer ~30 Hz.
//   - The Probe ring buffers are internally mutex-guarded (circular overwrite).
//
// No Qt headers are included here: this class is pure C++17/20.
//

#pragma once

#include <s7.hpp>
#include <s7probe.hpp>
#include <s7thread_pool.hpp>

#include <boost/json.hpp>

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace s7ui {

class SchemaSession
{
public:
    // ------------------------------------------------------------------
    // Factory
    // ------------------------------------------------------------------

    /// Build a session from a complete netlist JSON.
    /// 'sink_item_ids' lists the node IDs of Probe-type items; after build
    /// those elements are accessible via probe().
    ///
    /// Throws on parse / build errors.
    static std::shared_ptr<SchemaSession> build(
        const boost::json::value&        netlist_json,
        const std::vector<std::string>&  sink_item_ids);

    ~SchemaSession();

    // ------------------------------------------------------------------
    // Async execution
    // ------------------------------------------------------------------

    /// Start running the schema for 'ticks' steps in a background thread.
    /// Calls 'on_completed' on the background thread when done (not on GUI thread!).
    /// Use a thread-safe mechanism (e.g. QMetaObject::invokeMethod) if you
    /// need to update the GUI from the callback.
    void runAsync(long ticks,
                  std::function<void(bool /*succeeded*/, const std::string& /*error*/)> on_completed = {});

    void pause();
    void resume();
    void stop();

    /// Block until the background thread has finished.  Safe to call from GUI.
    void waitForCompletion();

    // ------------------------------------------------------------------
    // Status
    // ------------------------------------------------------------------

    [[nodiscard]] bool isRunning()  const { return running_.load(); }
    [[nodiscard]] bool isPaused()   const { return paused_.load();  }
    [[nodiscard]] long completedTicks() const { return completed_ticks_.load(); }
    [[nodiscard]] long totalTicks()     const { return total_ticks_; }

    // ------------------------------------------------------------------
    // Data access
    // ------------------------------------------------------------------

    /// Access the Real probe for a given sink item ID.  Returns nullptr if not found.
    [[nodiscard]] std::shared_ptr<s7::Probe> probe(const std::string& item_id) const;
    /// Access the Complex probe for a given sink item ID.  Returns nullptr if not found.
    [[nodiscard]] std::shared_ptr<s7::ComplexProbe> complexProbe(const std::string& item_id) const;

    /// Direct Schema access (for hot-patch operations, e.g. inserting probes
    /// while paused).
    [[nodiscard]] std::shared_ptr<s7::Schema> schema() const { return schema_; }

private:
    SchemaSession() = default;

    std::shared_ptr<s7::Schema>               schema_;
    std::unordered_map<std::string,
                       std::shared_ptr<s7::Probe>>  probes_; // item_id → probe
    std::unordered_map<std::string,
                       std::shared_ptr<s7::ComplexProbe>> complex_probes_; // item_id → complex probe

    long               total_ticks_     = 0;
    std::atomic<long>  completed_ticks_ = 0;
    std::atomic<bool>  running_         = false;
    std::atomic<bool>  paused_          = false;
    std::atomic<bool>  stop_requested_  = false;

    std::shared_ptr<s7::ThreadPool> compute_pool_;
    std::future<void> run_future_;
};

} // namespace s7ui

