// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/rate_inference.hpp"
#include "s7/resampler.hpp"

#include "s7/element.hpp"
#include "s7/schema.hpp"
#include "s7/validation.hpp"

#include <numeric>
#include <optional>

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

    // Iterative with path compression, so a long chain cannot exhaust the stack.
    std::size_t find(std::size_t index)
    {
        auto root = index;
        while (parent_[root] != root) root = parent_[root];
        while (parent_[index] != root) {
            const auto next = parent_[index];
            parent_[index] = root;
            index = next;
        }
        return root;
    }

    void merge(const std::size_t a, const std::size_t b) { parent_[find(a)] = find(b); }

private:
    std::vector<std::size_t> parent_;
};

} // namespace

namespace {

const detail::SchemaPort* schema_port(const std::unordered_map<std::string, const Element*>& instances, const std::string& id)
{
    const auto it = instances.find(id);
    return it == instances.end() ? nullptr : dynamic_cast<const detail::SchemaPort*>(it->second);
}

// A flattened nested schema's port declares the schema's own rate: name the
// schema, not the port.
std::string rate_label(const std::unordered_map<std::string, const Element*>& instances, const std::string& id)
{
    if (const auto* port = schema_port(instances, id)) return "nested schema '" + port->owner() + "'";
    return "'" + id + "'";
}

} // namespace

ResolvedRates RateResolver::resolve(const Schema& schema, const std::unordered_map<std::string, const Element*>& instances,
                                    const std::unordered_map<std::string, Rate>& seeds)
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
        // A rate-changing element (Resampler) joins two rate domains: its input
        // connection belongs to the upstream domain, not to its own.
        if (const auto target = instances.find(connection.to_element);
            target != instances.end() && dynamic_cast<const RateChangingElement*>(target->second)) {
            continue;
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
            result.errors.push_back("Rate conflict: " + rate_label(instances, elements[index].id) + " declares " +
                                     declared->to_string() + " but " + rate_label(instances, root_pinned_by.at(root)) +
                                     ", connected to it, declares " + existing->second.to_string() +
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
        // Without a pin, the seed of the schema (or nested schema) the members
        // belong to. A set crossing into a nested schema with a rate of its own
        // is pinned by its ports, so the members' seeds agree.
        std::optional<Rate> seed;
        for (const auto& id : members) {
            const auto own = seeds.find(id);
            const auto member_seed = own != seeds.end() ? std::optional{own->second} : schema.rate();
            if (!member_seed) continue;
            if (seed && *seed != *member_seed) {
                result.errors.push_back("The rate of '" + id + "' is seeded with both " + seed->to_string() + " and " +
                                         member_seed->to_string() + ".");
            }
            seed = member_seed;
        }
        if (seed) {
            root_rate.emplace(root, *seed);
            continue;
        }
        std::string names;
        std::string first;
        for (const auto& id : members) {
            if (schema_port(instances, id)) continue; // wires: name the elements
            if (first.empty()) first = id;
            names += (names.empty() ? "" : ", ") + id;
        }
        if (first.empty()) first = members.front();
        result.errors.push_back("The rate of '" + first + "' cannot be determined (connected to: " + names +
                                 "). Set the run's dt (the schema's rate), or connect an element whose rate is fixed, such as a file, stream or recording.");
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
