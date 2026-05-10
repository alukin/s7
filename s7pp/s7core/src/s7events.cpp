#include "s7events.hpp"
#include <algorithm>
namespace s7 {
uint64_t EventBus::subscribe(const std::vector<std::string>& topics,
                             Subscriber callback)
{
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    uint64_t sub_id = next_subscription_id_++;
    subscriptions_.push_back(Subscription{sub_id, topics, callback});
    return sub_id;
}
void EventBus::unsubscribe(uint64_t subscription_id)
{
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    auto it = std::find_if(subscriptions_.begin(), subscriptions_.end(),
                           [subscription_id](const Subscription& s) {
                               return s.id == subscription_id;
                           });
    if (it != subscriptions_.end()) {
        subscriptions_.erase(it);
    }
}
void EventBus::publish(const Event& event)
{
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    for (const auto& sub : subscriptions_) {
        for (const auto& topic : sub.topics) {
            if (topicMatches(event.event_name, topic)) {
                sub.callback(event);
                break;
            }
        }
    }
}
void EventBus::publishEvent(const std::string& event_name,
                           const nlohmann::json& payload)
{
    Event evt;
    evt.event_name = event_name;
    evt.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    evt.payload = payload;
    publish(evt);
}
bool EventBus::topicMatches(const std::string& event_name,
                            const std::string& topic)
{
    if (event_name == topic) return true;
    if (event_name.substr(0, topic.length()) == topic &&
        event_name.length() > topic.length() &&
        event_name[topic.length()] == '.') {
        return true;
    }
    return false;
}
} // namespace s7
