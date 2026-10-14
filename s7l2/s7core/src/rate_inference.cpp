#include "s7/rate_inference.hpp"

#include "s7/element.hpp"
#include "s7/schema.hpp"

#include <numeric>

namespace s7 {

namespace {

// Plain union-find over schema elements (not ports: every element today has
// exactly one native rate shared by all its ports -- see the header comment).
class DisjointSet {
public:
    explicit DisjointSet(const std::size_t size) : parent_(size)
    {
        std::iota(parent_.begin(), parent_.end(), std::size_t{0});
    }

    std::size_t find(const std::size_t index)
    {
        if (parent_[index] != index) {
            parent_[index] = find(parent_[index]);
        }
        return parent_[index];
    }

    void merge(const std::size_t a, const std::size_t b) { parent_[find(a)] = find(b); }

private:
    std::vector<std::size_t> parent_;
};

} // namespace

ResolvedRates RateResolver::resolve(const Schema& schema, const std::unordered_map<std::string, const Element*>& instances)
{
    ResolvedRates result;
    const auto& elements = schema.elements();

    std::unordered_map<std::string, std::size_t> index_of;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        index_of[elements[index].id] = index;
    }

    DisjointSet sets(elements.size());
    for (const auto& connection : schema.connections()) {
        const auto from_it = index_of.find(connection.from_element);
        const auto to_it = index_of.find(connection.to_element);
        if (from_it == index_of.end() || to_it == index_of.end()) {
            continue; // reported by validation
        }
        sets.merge(from_it->second, to_it->second);
    }

    // Per root: its resolved rate (once decided) and the element that pinned
    // it, for conflict messages; and every member id, for an "undetermined" error.
    std::unordered_map<std::size_t, Rate> root_rate;
    std::unordered_map<std::size_t, std::string> root_pinned_by;
    std::unordered_map<std::size_t, std::vector<std::string>> root_members;

    for (std::size_t index = 0; index < elements.size(); ++index) {
        const auto root = sets.find(index);
        root_members[root].push_back(elements[index].id);

        const auto instance_it = instances.find(elements[index].id);
        if (instance_it == instances.end()) {
            continue; // reported elsewhere (duplicate id, factory error, ...)
        }
        const auto declared = instance_it->second->declared_rate();
        if (!declared) {
            continue;
        }
        const auto existing = root_rate.find(root);
        if (existing == root_rate.end()) {
            root_rate.emplace(root, *declared);
            root_pinned_by.emplace(root, elements[index].id);
        } else if (!(existing->second == *declared)) {
            result.errors.push_back("Rate conflict: '" + elements[index].id + "' declares " + declared->to_string() + " but '" +
                                     root_pinned_by.at(root) + "', connected to it, declares " + existing->second.to_string() +
                                     ". Insert a rate-changing element.");
        }
    }

    if (!result.errors.empty()) {
        return result;
    }

    for (const auto& [root, members] : root_members) {
        if (root_rate.contains(root)) {
            continue;
        }
        if (const auto seed = schema.rate()) {
            root_rate.emplace(root, *seed);
            continue;
        }
        std::string names;
        for (const auto& id : members) {
            names += (names.empty() ? "" : ", ") + id;
        }
        result.errors.push_back("The rate of '" + members.front() + "' cannot be determined (connected to: " + names +
                                 "). Pin a rate on one of them (e.g. an integrator's \"dt\"), or set the schema's rate.");
    }

    if (!result.errors.empty()) {
        return result;
    }

    for (std::size_t index = 0; index < elements.size(); ++index) {
        result.rates.emplace(elements[index].id, root_rate.at(sets.find(index)));
    }
    return result;
}

} // namespace s7
