#include <fem/core/element_type.hpp>

#include <array>
#include <cstddef>

namespace fem::core {

namespace {

#include "element_reference_coordinates.inc"

constexpr std::array<ElementTypeInfo, kElementTypeCount> kInfos = {{
#define FEM_ELEMENT_TYPE(name, family, dimension, order, nodeCount, vertexCount, gmshType)         \
    {#name, ElementFamily::family, dimension, order, nodeCount, vertexCount, gmshType},
#include <fem/core/element_types.inc>
#undef FEM_ELEMENT_TYPE
}};

constexpr std::array<ElementType, kElementTypeCount> kTypes = {{
#define FEM_ELEMENT_TYPE(name, family, dimension, order, nodeCount, vertexCount, gmshType)         \
    ElementType::name,
#include <fem/core/element_types.inc>
#undef FEM_ELEMENT_TYPE
}};

const std::array<std::span<const std::array<double, 3>>, kElementTypeCount> kReferences = {{
#define FEM_ELEMENT_TYPE(name, family, dimension, order, nodeCount, vertexCount, gmshType)         \
    kReference##name,
#include <fem/core/element_types.inc>
#undef FEM_ELEMENT_TYPE
}};

constexpr std::size_t indexOf(ElementType type) noexcept
{
    return static_cast<std::size_t>(type);
}

} // namespace

const ElementTypeInfo& elementTypeInfo(ElementType type) noexcept
{
    return kInfos[indexOf(type)];
}

std::span<const ElementType> allElementTypes() noexcept
{
    return kTypes;
}

std::span<const std::array<double, 3>> referenceNodeCoordinates(ElementType type) noexcept
{
    return kReferences[indexOf(type)];
}

} // namespace fem::core
