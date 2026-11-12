#pragma once

#include <fem/core/expected.hpp>
#include <fem/core/mesh.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fem::io {

struct GmshReadResult
{
    core::Mesh mesh;
    std::vector<std::string> warnings; // e.g. ignored element types
};

// Reads a Gmsh .msh file in format 4.1, ASCII or binary (REQ-003-15 to 24):
// nodes, elements of the supported types and physical groups. On failure the
// error says what is wrong and where; no partial mesh is returned.
[[nodiscard]] core::Expected<GmshReadResult> readGmsh(const std::filesystem::path& path);

// Same, from the contents of a file. `sourceName` only appears in messages.
[[nodiscard]] core::Expected<GmshReadResult> readGmsh(std::string_view contents,
                                                      std::string_view sourceName);

} // namespace fem::io
