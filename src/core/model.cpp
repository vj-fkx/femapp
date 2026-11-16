#include <fem/core/model.hpp>

#include <algorithm>
#include <string>

namespace fem::core {

namespace {

template <typename T>
const T* findByName(const std::vector<T>& items, std::string_view name)
{
    const auto it = std::ranges::find(items, name, &T::name);
    return it == items.end() ? nullptr : &*it;
}

template <typename Index>
void sortUnique(std::vector<Index>& indices)
{
    std::ranges::sort(indices);
    const auto tail = std::ranges::unique(indices);
    indices.erase(tail.begin(), tail.end());
}

} // namespace

const Material* Model::findMaterial(std::string_view name) const
{
    return findByName(materials, name);
}

const Section* Model::findSection(std::string_view name) const
{
    return findByName(sections, name);
}

Expected<std::vector<ElementIndex>> resolve(const Mesh& mesh, const ElementTarget& target)
{
    if (const auto* group = std::get_if<GroupRef>(&target)) {
        const Group* found = mesh.findGroup(group->name, 1);
        if (found == nullptr) {
            return makeError("The mesh has no group \"" + group->name + "\" of lines.");
        }
        return found->elements;
    }
    std::vector<ElementIndex> elements;
    for (const ExternalId id : std::get<ElementIds>(target).ids) {
        const auto element = mesh.findElement(id);
        if (!element) {
            return makeError("The mesh has no element " + std::to_string(id) + ".");
        }
        elements.push_back(*element);
    }
    sortUnique(elements);
    return elements;
}

Expected<std::vector<NodeIndex>> resolve(const Mesh& mesh, const NodeTarget& target)
{
    std::vector<NodeIndex> nodes;
    if (const auto* group = std::get_if<GroupRef>(&target)) {
        const auto groups = mesh.findGroups(group->name);
        if (groups.empty()) {
            return makeError("The mesh has no group \"" + group->name + "\".");
        }
        for (const Group* found : groups) {
            const auto groupNodes = mesh.groupNodes(*found);
            nodes.insert(nodes.end(), groupNodes.begin(), groupNodes.end());
        }
    } else {
        for (const ExternalId id : std::get<NodeIds>(target).ids) {
            const auto node = mesh.findNode(id);
            if (!node) {
                return makeError("The mesh has no node " + std::to_string(id) + ".");
            }
            nodes.push_back(*node);
        }
    }
    sortUnique(nodes);
    return nodes;
}

} // namespace fem::core
