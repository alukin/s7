//
// s7events.hpp — EventBus for control plane event distribution
//
// Provides publish/subscribe mechanism for schema lifecycle events:
//   - schema.loaded, schema.unloaded
//   - run.started, run.paused, run.resumed, run.completed, run.stopped, run.failed
//   - runtime.warning, runtime.error
//
// Thread-safe: uses internal mutex for subscription management.
//

#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/json.hpp>

namespace s7 {

/// Event envelope published by EventBus.
struct Event {
    std::string event_name;
    long timestamp_ms;
    boost::json::value payload;
};

/// EventBus — publish/subscribe system for control plane events.
class EventBus {
public:
    using Subscriber = std::function<void(const Event&)>;

    /// Subscribe to events matching any of the given topics.
    /// Returns a subscription ID for later unsubscribe.
    ///
    /// Example topics: "schema", "run", "runtime", "error", "warning"
    /// (Prefix matching: "run" matches "run.started", "run.paused", etc.)
    uint64_t subscribe(const std::vector<std::string>& topics,
                       Subscriber callback);

    /// Unsubscribe a previous subscription by its ID.
    void unsubscribe(uint64_t subscription_id);

    /// Publish an event to all matching subscribers.
    void publish(const Event& event);

    /// Helper: publish with automatic timestamp
    void publishEvent(const std::string& event_name,
                      const boost::json::value& payload);

private:
    struct Subscription {
        uint64_t id;
        std::vector<std::string> topics;
        Subscriber callback;
    };

    uint64_t next_subscription_id_ = 1;
    std::vector<Subscription> subscriptions_;
    mutable std::mutex subscribers_mutex_;

    /// Check if event_name matches any of the topics (prefix match)
    static bool topicMatches(const std::string& event_name,
                             const std::string& topic);
};

} // namespace s7

