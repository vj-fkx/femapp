#include <fem/mesher/line_mesher.hpp>

namespace fem::mesher {

std::string entityGroupName(geom::EntityId id)
{
    return "entity " + std::to_string(static_cast<std::uint64_t>(id));
}

tl::expected<LineMeshResult, LineMeshError> meshLines(const geom::Drawing& /*drawing*/,
                                                      const LineMeshOptions& /*options*/)
{
    // Feature 009, T01: the interfaces only; the generator comes in T06.
    return tl::unexpected(LineMeshError{.kind = LineMeshError::Kind::NoLines});
}

} // namespace fem::mesher
