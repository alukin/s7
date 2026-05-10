#include "s7netio.hpp"

#include <algorithm>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace s7 {
namespace {

struct ChannelState {
    std::size_t num_channels = 0;
    std::size_t queue_capacity = 0;
    std::deque<std::vector<double>> queue;
};

class NetBroker {
public:
    static NetBroker& instance()
    {
        static NetBroker broker;
        return broker;
    }

    void ensure(const std::string& key, std::size_t num_channels, std::size_t capacity)
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto& ch = channels_[key];
        if (ch.num_channels == 0) {
            ch.num_channels = num_channels;
            ch.queue_capacity = std::max<std::size_t>(1, capacity);
            return;
        }
        if (ch.num_channels != num_channels) {
            throw std::runtime_error("s7netio: endpoint channel-count mismatch for '" + key + "'");
        }
    }

    bool push(const std::string& key, std::vector<double> sample)
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = channels_.find(key);
        if (it == channels_.end()) {
            throw std::runtime_error("s7netio: endpoint not initialized: " + key);
        }
        auto& ch = it->second;
        bool dropped = false;
        if (ch.queue.size() >= ch.queue_capacity) {
            ch.queue.pop_front();
            dropped = true;
        }
        ch.queue.push_back(std::move(sample));
        return dropped;
    }

    bool pop(const std::string& key, std::vector<double>& out)
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = channels_.find(key);
        if (it == channels_.end() || it->second.queue.empty()) {
            return false;
        }
        out = std::move(it->second.queue.front());
        it->second.queue.pop_front();
        return true;
    }

private:
    std::mutex mu_;
    std::unordered_map<std::string, ChannelState> channels_;
};

void validateConfig(const NetIOConfig& cfg)
{
    if (cfg.endpoint.empty()) throw std::runtime_error("s7netio: endpoint must not be empty");
    if (cfg.num_channels == 0) throw std::runtime_error("s7netio: num_channels must be > 0");
    if (cfg.queue_capacity == 0) throw std::runtime_error("s7netio: queue_capacity must be > 0");
}

} // namespace

class RealNetSink::Impl {
public:
    explicit Impl(NetIOConfig cfg) : cfg_(std::move(cfg))
    {
        validateConfig(cfg_);
        NetBroker::instance().ensure(cfg_.endpoint, cfg_.num_channels, cfg_.queue_capacity);
    }

    NetIOConfig cfg_;
};

class RealNetSource::Impl {
public:
    explicit Impl(NetIOConfig cfg) : cfg_(std::move(cfg))
    {
        validateConfig(cfg_);
        NetBroker::instance().ensure(cfg_.endpoint, cfg_.num_channels, cfg_.queue_capacity);
    }

    NetIOConfig cfg_;
};

class ComplexNetSink::Impl {
public:
    explicit Impl(NetIOConfig cfg) : cfg_(std::move(cfg))
    {
        validateConfig(cfg_);
        NetBroker::instance().ensure(cfg_.endpoint, cfg_.num_channels * 2u, cfg_.queue_capacity);
    }

    NetIOConfig cfg_;
};

class ComplexNetSource::Impl {
public:
    explicit Impl(NetIOConfig cfg) : cfg_(std::move(cfg))
    {
        validateConfig(cfg_);
        NetBroker::instance().ensure(cfg_.endpoint, cfg_.num_channels * 2u, cfg_.queue_capacity);
    }

    NetIOConfig cfg_;
};

RealNetSink::RealNetSink(const std::string& id, const NetIOConfig& cfg)
    : Element(id), impl_(std::make_unique<Impl>(cfg))
{
    setTraits(kIsStateful, kHasDirectFeedthrough);
    inputs_.reserve(cfg.num_channels);
    for (uint32_t i = 0; i < cfg.num_channels; ++i) {
        inputs_.push_back(addInput<Real>());
    }
}

RealNetSink::~RealNetSink() = default;

void RealNetSink::tick()
{
    std::vector<double> sample;
    sample.reserve(inputs_.size());
    for (const auto& in : inputs_) {
        sample.push_back(in->value());
    }

    const bool dropped = NetBroker::instance().push(impl_->cfg_.endpoint, std::move(sample));
    ++pushed_samples_;
    if (dropped) ++dropped_samples_;
}

RealNetSource::RealNetSource(const std::string& id, const NetIOConfig& cfg)
    : Element(id), impl_(std::make_unique<Impl>(cfg)), values_(cfg.num_channels, 0.0)
{
    setTraits(kIsStateful, kHasDirectFeedthrough);
    for (auto& v : values_) {
        addOutput<Real>(v);
    }
}

RealNetSource::~RealNetSource() = default;

void RealNetSource::tick()
{
    std::vector<double> sample;
    if (!NetBroker::instance().pop(impl_->cfg_.endpoint, sample)) {
        ++underruns_;
        if (impl_->cfg_.zero_on_underrun) {
            std::fill(values_.begin(), values_.end(), 0.0);
        }
        return;
    }

    if (sample.size() != values_.size()) {
        throw std::runtime_error("s7netio: received sample width mismatch");
    }

    for (std::size_t i = 0; i < values_.size(); ++i) {
        values_[i] = sample[i];
    }
    ++pulled_samples_;
}

ComplexNetSink::ComplexNetSink(const std::string& id, const NetIOConfig& cfg)
    : Element(id), impl_(std::make_unique<Impl>(cfg))
{
    setTraits(kIsStateful, kHasDirectFeedthrough);
    inputs_.reserve(cfg.num_channels);
    for (uint32_t i = 0; i < cfg.num_channels; ++i) {
        inputs_.push_back(addInput<Complex>());
    }
}

ComplexNetSink::~ComplexNetSink() = default;

void ComplexNetSink::tick()
{
    std::vector<double> sample;
    sample.reserve(inputs_.size() * 2u);
    for (const auto& in : inputs_) {
        const auto v = in->value();
        sample.push_back(v.real());
        sample.push_back(v.imag());
    }

    const bool dropped = NetBroker::instance().push(impl_->cfg_.endpoint, std::move(sample));
    ++pushed_samples_;
    if (dropped) ++dropped_samples_;
}

ComplexNetSource::ComplexNetSource(const std::string& id, const NetIOConfig& cfg)
    : Element(id), impl_(std::make_unique<Impl>(cfg)), values_(cfg.num_channels, Complex{0.0, 0.0})
{
    setTraits(kIsStateful, kHasDirectFeedthrough);
    for (auto& v : values_) {
        addOutput<Complex>(v);
    }
}

ComplexNetSource::~ComplexNetSource() = default;

void ComplexNetSource::tick()
{
    std::vector<double> sample;
    if (!NetBroker::instance().pop(impl_->cfg_.endpoint, sample)) {
        ++underruns_;
        if (impl_->cfg_.zero_on_underrun) {
            std::fill(values_.begin(), values_.end(), Complex{0.0, 0.0});
        }
        return;
    }

    if (sample.size() != values_.size() * 2u) {
        throw std::runtime_error("s7netio: received complex sample width mismatch");
    }

    for (std::size_t i = 0; i < values_.size(); ++i) {
        values_[i] = Complex{sample[2u * i], sample[2u * i + 1u]};
    }
    ++pulled_samples_;
}

} // namespace s7


