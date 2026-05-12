#pragma once

#include <complex>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace s7 {

using Real = double;
using Complex = std::complex<double>;

class Element;
class ThreadPool;

enum class ElementEventSeverity {
    Info,
    Warning,
    Critical,
};

struct ElementEvent {
    std::string topic;
    ElementEventSeverity severity = ElementEventSeverity::Info;
    std::string message;
};

// -------------------------------------------------------------------------
// IElementObserver — observer interface for element event monitoring.
// Implementations must be thread-safe: onEvent() is called from the compute
// thread (which may differ from the GUI thread).
// In GUI mode: use a ring buffer bridge to the GUI thread.
// In headless mode: use for logging / streaming.
// -------------------------------------------------------------------------
class IElementObserver {
public:
    virtual ~IElementObserver() = default;

    /// Called from the compute thread when an element emits an event.
    /// Do not call back into the Schema from this method.
    virtual void onEvent(const Element& element, const ElementEvent& event) {}
};

class Pin {
public:
    virtual ~Pin() = default;
    explicit Pin() = default;
    Element* owner = nullptr;
    std::string name;
    std::string description;
};

class Output : public Pin {
public:
    ~Output() override = default;
    [[nodiscard]] virtual auto debugValueString() const -> std::string = 0;
};

template <typename T>
class TypedOutput final : public Output {
public:
    explicit TypedOutput(T& value_ref) : val_(value_ref) {}

    [[nodiscard]] auto debugValueString() const -> std::string override
    {
        std::ostringstream stream;
        stream << val_;
        return stream.str();
    }

    [[nodiscard]] auto value() const -> T& { return val_; }

private:
    T& val_;
};

using RealOutput    = TypedOutput<Real>;
using ComplexOutput = TypedOutput<Complex>;

class Input : public Pin {
public:
    ~Input() override = default;
    virtual auto connect(std::shared_ptr<Output> output) -> bool = 0;
    [[nodiscard]] virtual auto isConnected() const -> bool = 0;
};

template <typename T>
class TypedInput final : public Input {
public:
    auto connect(std::shared_ptr<Output> output) -> bool override
    {
        if (connected_to_) return false;
        if (!output)
            throw std::invalid_argument("Cannot connect to null output");

        auto typed = std::dynamic_pointer_cast<TypedOutput<T>>(output);
        if (!typed)
            throw std::runtime_error("Signal type mismatch: incompatible pin types");

        connected_to_ = std::move(typed);
        return true;
    }

    [[nodiscard]] auto isConnected() const -> bool override { return connected_to_ != nullptr; }

    [[nodiscard]] auto value() const -> T
    {
        if (!connected_to_)
            throw std::runtime_error("Input is not connected");
        return connected_to_->value();
    }

private:
    std::shared_ptr<TypedOutput<T>> connected_to_ = nullptr;
};

using RealInput    = TypedInput<Real>;
using ComplexInput = TypedInput<Complex>;

class IdGenerator {
public:
    auto operator()() const -> std::string;
};

class Schema;

class SchemaExecutionError : public std::runtime_error {
public:
    explicit SchemaExecutionError(const std::string& message,
                                  std::vector<std::string> cycle_nodes = {},
                                  std::vector<std::string> cycle_edges = {})
        : std::runtime_error(message),
          cycle_nodes_(std::move(cycle_nodes)),
          cycle_edges_(std::move(cycle_edges))
    {
    }

    [[nodiscard]] auto cycleNodes() const -> const std::vector<std::string>&
    {
        return cycle_nodes_;
    }

    [[nodiscard]] auto cycleEdges() const -> const std::vector<std::string>&
    {
        return cycle_edges_;
    }

private:
    std::vector<std::string> cycle_nodes_;
    std::vector<std::string> cycle_edges_;
};

class Element {
public:
    Element();
    explicit Element(std::string id);
    virtual ~Element() = default;

    std::weak_ptr<Schema> owner{};

    std::vector<std::shared_ptr<Input>>  inputs;
    std::vector<std::shared_ptr<Output>> outputs;

    virtual void tick() = 0;
    virtual void calculateParameters() {}
    virtual void commit() {}

    [[nodiscard]] virtual auto isStateful() const -> bool;
    [[nodiscard]] virtual auto hasDirectFeedthrough() const -> bool;

    [[nodiscard]] auto isSource() const -> bool;
    [[nodiscard]] auto isSink() const -> bool;

    [[nodiscard]] auto getId() const -> std::string;

    auto getInput(int idx) -> std::shared_ptr<Input>;
    auto getOutput(int idx) -> std::shared_ptr<Output>;

    /// Attach an observer.  The observer is held as a weak_ptr so it does not
    /// extend the element's lifetime, and vice-versa.
    void setObserver(std::shared_ptr<IElementObserver> observer);
    void clearObserver();

protected:
    void setTraits(bool is_stateful, std::optional<bool> has_direct_feedthrough = std::nullopt);

    /// Notify the observer (if any) with an explicit event payload.
    void notifyObserver(const ElementEvent& event);

    template<typename T>
    auto addInput() -> std::shared_ptr<TypedInput<T>>
    {
        auto pin = std::make_shared<TypedInput<T>>();
        pin->owner = this;
        inputs.push_back(pin);
        return pin;
    }

    template<typename T>
    auto addOutput(T& value_ref) -> std::shared_ptr<TypedOutput<T>>
    {
        auto pin = std::make_shared<TypedOutput<T>>(value_ref);
        pin->owner = this;
        outputs.push_back(pin);
        return pin;
    }

private:
    std::string id_;
    bool is_stateful_ = false;
    std::optional<bool> has_direct_feedthrough_;
    std::weak_ptr<IElementObserver> observer_;
};

using ElementPtr = std::shared_ptr<Element>;

class Schema : public std::enable_shared_from_this<Schema>, public Element
{
public:
    explicit Schema(double td);
    explicit Schema(double td, std::string id);

    void tick() override;
    void calculateParameters() override;
    void insert(const ElementPtr& e);
    auto connect(const ElementPtr& el1, int output_idx,
                 const ElementPtr& el2, int input_idx) -> bool;
    void prepareExecution();
    void tickWithThreadPool(ThreadPool& pool, bool parallel_stages = true);
    void run(long ticks);
    void runWithThreadPool(long ticks, ThreadPool& pool, bool parallel_stages = true);
    void run_s(double seconds);

    [[nodiscard]] auto get_TD() const -> double;
    [[nodiscard]] auto getElementById(const std::string& id) const -> ElementPtr;
    [[nodiscard]] auto elementIds() const -> const std::vector<std::string>&;

private:
    struct Connection {
        std::string from_id;
        int output_idx;
        std::string to_id;
        int input_idx;
    };

    void rebuildExecutionPlan();

    std::unordered_map<std::string, ElementPtr> elements;
    std::vector<std::string>  insertion_order;
    std::vector<Connection>   connections;
    std::vector<ElementPtr>   execution_order;
    std::vector<std::vector<ElementPtr>> execution_stages;
    bool   execution_plan_dirty = true;
    double discr_time;
};

} // namespace s7
