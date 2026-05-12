//
// schema_session.cpp
//

#include "schema_session.hpp"

#include <s7netlist.hpp>
#include <s7factory.hpp>

#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <thread>

namespace s7ui {

// static
std::shared_ptr<SchemaSession> SchemaSession::build(
    const boost::json::value&       netlist_json,
    const std::vector<std::string>& sink_item_ids)
{
    auto session = std::shared_ptr<SchemaSession>(new SchemaSession());

    // Build the live Schema from the netlist JSON.
    const auto doc    = s7::SchemaBuilder::parseJson(netlist_json);
    session->schema_  = s7::SchemaBuilder::build(doc);

    const auto hw_threads = std::thread::hardware_concurrency();
    const std::size_t worker_count = std::max<std::size_t>(2, hw_threads > 0 ? hw_threads : 2);
    session->compute_pool_ = std::make_shared<s7::ThreadPool>(worker_count);

    // Extract probe elements for every known sink item ID.
    for (const auto& item_id : sink_item_ids)
    {
        auto el = session->schema_->getElementById(item_id);
        if (!el)
        {
            continue;
        }

        if (auto probe = std::dynamic_pointer_cast<s7::Probe>(el))
        {
            session->probes_[item_id] = probe;
            continue;
        }

        if (auto cprobe = std::dynamic_pointer_cast<s7::ComplexProbe>(el))
        {
            session->complex_probes_[item_id] = cprobe;
        }
    }

    return session;
}

SchemaSession::~SchemaSession()
{
    stop();
    waitForCompletion();
}

void SchemaSession::runAsync(long ticks,
    std::function<void(bool, const std::string&)> on_completed)
{
    if (running_.load())
    {
        if (on_completed)
        {
            on_completed(false, "already running");
        }
        return;
    }

    total_ticks_    = ticks;
    completed_ticks_.store(0);
    stop_requested_.store(false);
    paused_.store(false);
    running_.store(true);

    // Clear probe buffers before a fresh run.
    for (auto& [id, probe] : probes_)
    {
        probe->clearBuffers();
    }
    for (auto& [id, probe] : complex_probes_)
    {
        probe->clearBuffers();
    }

    run_future_ = compute_pool_->submit([this, on_completed] {
        std::string error_msg;
        bool        succeeded = true;

        try
        {
            schema_->prepareExecution();
            for (long i = 0; i < total_ticks_; ++i)
            {
                if (stop_requested_.load()) break;

                // Spin-wait while paused (low-overhead; pause is infrequent).
                while (paused_.load() && !stop_requested_.load())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                if (stop_requested_.load()) break;

                schema_->tickWithThreadPool(*compute_pool_, true);
                completed_ticks_.store(i + 1);
            }
        }
        catch (const std::exception& ex)
        {
            succeeded  = false;
            error_msg  = ex.what();
        }

        running_.store(false);

        if (on_completed)
        {
            on_completed(succeeded, error_msg);
        }
    });
}

void SchemaSession::pause()
{
    paused_.store(true);
}

void SchemaSession::resume()
{
    paused_.store(false);
}

void SchemaSession::stop()
{
    stop_requested_.store(true);
    paused_.store(false); // unblock the pause-spin
}

void SchemaSession::waitForCompletion()
{
    if (run_future_.valid())
    {
        run_future_.get();
    }
}

std::shared_ptr<s7::Probe> SchemaSession::probe(const std::string& item_id) const
{
    const auto it = probes_.find(item_id);
    return it != probes_.end() ? it->second : nullptr;
}

std::shared_ptr<s7::ComplexProbe> SchemaSession::complexProbe(const std::string& item_id) const
{
    const auto it = complex_probes_.find(item_id);
    return it != complex_probes_.end() ? it->second : nullptr;
}

} // namespace s7ui

