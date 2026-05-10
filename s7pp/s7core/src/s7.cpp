#include "s7.hpp"

#include <algorithm>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <map>
#include <set>
#include <stdexcept>
#include <sstream>

namespace s7 {

auto IdGenerator::operator()() const -> std::string
{
    auto uuid = boost::uuids::random_generator()();
    std::stringstream stream;
    stream << uuid;
    return stream.str();
}

Element::Element() : id_(IdGenerator()()) {}

Element::Element(std::string id)
    : id_(id.empty() ? IdGenerator()() : std::move(id))
{
}

auto Element::isStateful() const -> bool
{
    return is_stateful_;
}

auto Element::hasDirectFeedthrough() const -> bool
{
    return has_direct_feedthrough_.value_or(!inputs.empty());
}

void Element::setTraits(bool is_stateful, std::optional<bool> has_direct_feedthrough)
{
    is_stateful_ = is_stateful;
    has_direct_feedthrough_ = has_direct_feedthrough;
}

auto Element::isSource() const -> bool
{
    return inputs.empty();
}

auto Element::isSink() const -> bool
{
    return outputs.empty();
}

auto Element::getId() const -> std::string
{
    return id_;
}

auto Element::getInput(int idx) -> std::shared_ptr<Input>
{
    return inputs.at(idx);
}

auto Element::getOutput(int idx) -> std::shared_ptr<Output>
{
    return outputs.at(idx);
}

Schema::Schema(double td) : discr_time(td) {}

Schema::Schema(double td, std::string id)
    : Element(std::move(id)), discr_time(td)
{
}

auto Schema::get_TD() const -> double
{
    return discr_time;
}

auto Schema::getElementById(const std::string& id) const -> ElementPtr
{
    const auto it = elements.find(id);
    return it != elements.end() ? it->second : nullptr;
}

auto Schema::elementIds() const -> const std::vector<std::string>&
{
    return insertion_order;
}

namespace {
    auto joinIds(const std::vector<std::string>& ids) -> std::string
    {
        std::ostringstream stream;
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            if (i > 0)
            {
                stream << ", ";
            }
            stream << ids[i];
        }
        return stream.str();
    }

    auto joinEdgeDetails(const std::vector<std::string>& edges) -> std::string
    {
        std::ostringstream stream;
        for (std::size_t i = 0; i < edges.size(); ++i)
        {
            if (i > 0)
            {
                stream << "; ";
            }
            stream << edges[i];
        }
        return stream.str();
    }
}

void Schema::rebuildExecutionPlan()
{
    execution_order.clear();

    std::unordered_map<std::string, std::size_t> indegree;
    std::unordered_map<std::string, std::set<std::string>> adjacency;
    std::unordered_map<std::string, std::map<std::string, std::string>> edge_details;

    for (const auto& [id, _] : elements)
    {
        indegree[id] = 0;
        adjacency[id] = {};
        edge_details[id] = {};
    }

    for (const auto& connection : connections)
    {
        const auto source_it = elements.find(connection.from_id);
        const auto target_it = elements.find(connection.to_id);
        if (source_it == elements.end() || target_it == elements.end())
        {
            continue;
        }

        const auto& source = source_it->second;
        const auto& target = target_it->second;

        if (!target->hasDirectFeedthrough())
        {
            continue;
        }

        auto& outgoing = adjacency[source->getId()];
        if (outgoing.insert(target->getId()).second)
        {
            ++indegree[target->getId()];
            edge_details[source->getId()][target->getId()] =
                source->getId() + ":" + std::to_string(connection.output_idx) +
                " -> " +
                target->getId() + ":" + std::to_string(connection.input_idx);
        }
    }

    std::set<std::string> ready;
    for (const auto& [id, _] : indegree)
    {
        if (indegree[id] == 0)
        {
            ready.insert(id);
        }
    }

    std::vector<ElementPtr> ordered;
    while (!ready.empty())
    {
        const auto current_id = *ready.begin();
        ready.erase(ready.begin());
        ordered.push_back(elements.at(current_id));

        for (const auto& next_id : adjacency[current_id])
        {
            auto& next_indegree = indegree[next_id];
            --next_indegree;
            if (next_indegree == 0)
            {
                ready.insert(next_id);
            }
        }
    }

    if (ordered.size() != indegree.size())
    {
        std::vector<std::string> cycle_nodes;
        for (const auto& [id, in_degree] : indegree)
        {
            if (in_degree > 0)
            {
                cycle_nodes.push_back(id);
            }
        }

        if (cycle_nodes.empty())
        {
            for (const auto& [id, _] : indegree)
            {
                cycle_nodes.push_back(id);
            }
        }

        std::set<std::string> cycle_node_set(cycle_nodes.begin(), cycle_nodes.end());
        std::vector<std::string> cycle_edges;
        for (const auto& from_id : cycle_nodes)
        {
            for (const auto& to_id : adjacency[from_id])
            {
                if (!cycle_node_set.contains(to_id))
                {
                    continue;
                }
                const auto details_it = edge_details[from_id].find(to_id);
                if (details_it != edge_details[from_id].end())
                {
                    cycle_edges.push_back(details_it->second);
                }
            }
        }

        const auto nodes_text = joinIds(cycle_nodes);
        const auto edges_text = cycle_edges.empty() ? std::string{"<unavailable>"} : joinEdgeDetails(cycle_edges);

        throw SchemaExecutionError(
            "Schema contains an algebraic loop among current-sample direct-feedthrough dependencies: " +
            nodes_text + " | edges: " + edges_text,
            std::move(cycle_nodes),
            std::move(cycle_edges));
    }

    execution_order = std::move(ordered);
    execution_plan_dirty = false;
}

void Schema::tick()
{
    if (execution_plan_dirty)
    {
        throw SchemaExecutionError(
            "Schema execution plan is dirty. Call prepareExecution() or run() before tick().");
    }

    for (const auto& element : execution_order)
    {
        element->tick();
    }
}

void Schema::calculateParameters()
{
    if (execution_plan_dirty)
    {
        rebuildExecutionPlan();
    }

    for (const auto& element : execution_order)
    {
        element->calculateParameters();
    }
}

void Schema::prepareExecution()
{
    calculateParameters();
}

void Schema::insert(const ElementPtr& e)
{
  if (e == nullptr)
  {
      throw std::invalid_argument("Cannot insert a null element into schema");
  }

  const auto id = e->getId();
  if (elements.contains(id))
  {
      throw std::runtime_error("Schema already contains an element with id '" + id + "'");
  }

  elements[id] = e;
  insertion_order.push_back(id);
  e->owner = shared_from_this();
  execution_plan_dirty = true;
}

auto Schema::connect(const ElementPtr& el1, int output_idx, const ElementPtr& el2, int input_idx) -> bool
{
  if (el1 == nullptr || el2 == nullptr)
  {
      throw std::invalid_argument("Cannot connect null elements");
  }

  if (!elements.contains(el1->getId()) || !elements.contains(el2->getId()))
  {
      throw std::runtime_error("Both elements must be inserted into the schema before connecting them");
  }

  auto in = el2->getInput(input_idx);
  auto out = el1->getOutput(output_idx);
  if (!in->connect(out))
  {
      return false;
  }

  connections.push_back({
      .from_id = el1->getId(),
      .output_idx = output_idx,
      .to_id = el2->getId(),
      .input_idx = input_idx,
  });
  execution_plan_dirty = true;
  return true;
}

void Schema::run(long ticks)
{
    if (ticks < 0)
    {
        throw std::invalid_argument("Schema::run expects a non-negative tick count");
    }

    prepareExecution();

    for (long tick_idx = 0; tick_idx < ticks; ++tick_idx)
    {
        tick();
    }
}

void Schema::run_s(double seconds)
{
    run(lround(seconds/discr_time));
}
}
