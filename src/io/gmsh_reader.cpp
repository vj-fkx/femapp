#include "gmsh_cursor.hpp"

#include <fem/core/element_type.hpp>
#include <fem/core/mesh_builder.hpp>
#include <fem/io/gmsh_reader.hpp>

#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fem::io {

namespace {

using core::ElementHandle;
using core::ElementType;
using core::Expected;
using detail::GmshCursor;

constexpr std::string_view kSupportedVersion = "4.1";
constexpr int kSizeOfSizeT = 8;
constexpr std::int32_t kEndiannessCheck = 1;

using EntityKey = std::pair<int, int>; // (dimension, entity or physical tag)

// Number of nodes of the fixed-size Gmsh types femapp does not support, so
// that binary files containing them can still be read (REQ-003-19): the
// order-0 one-node elements and the types Gmsh 4.15.2 cannot create (design
// section 5.5).
const std::map<int, int>& unsupportedNodeCounts()
{
    static const std::map<int, int> counts = {
        {84, 1},   {85, 1},   {86, 1},   {87, 1},   {88, 1},   {89, 1},    {132, 1},
        {100, 44}, {101, 56}, {102, 68}, {103, 80}, {104, 92}, {105, 104}, {125, 21},
        {126, 29}, {127, 37}, {128, 45}, {129, 53}, {130, 61}, {131, 69},
    };
    return counts;
}

std::optional<ElementType> supportedType(int gmshType)
{
    static const std::map<int, ElementType> types = [] {
        std::map<int, ElementType> result;
        for (const ElementType type : core::allElementTypes()) {
            result.emplace(core::elementTypeInfo(type).gmshType, type);
        }
        return result;
    }();
    const auto it = types.find(gmshType);
    if (it == types.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string pathToString(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// Reads one .msh 4.1 file into a mesh.
class GmshParser
{
public:
    GmshParser(std::string_view contents, std::string_view sourceName)
        : m_cursor(contents, sourceName)
    {}

    Expected<GmshReadResult> run();

private:
    Expected<void> readMeshFormat();
    Expected<void> readPhysicalNames();
    Expected<void> readEntities();
    Expected<void> readNodes();
    Expected<void> readElements();
    Expected<void> addGroups();

    // Error at the cursor position.
    [[nodiscard]] tl::unexpected<core::Error> fail(std::string_view what) const
    {
        return tl::unexpected(m_cursor.error(what));
    }

    GmshCursor m_cursor;
    bool m_sawFormat = false;
    bool m_binary = false;
    core::MeshBuilder m_builder;
    std::map<EntityKey, std::string> m_physicalNames;
    std::map<EntityKey, std::vector<int>> m_entityPhysicals;
    std::map<EntityKey, std::vector<ElementHandle>> m_entityElements;
    std::map<int, std::uint64_t> m_ignored; // Gmsh type -> element count
    std::uint64_t m_supportedElements = 0;
};

// Propagates the error of an Expected expression.
#define FEM_TRY(expression)                                                                        \
    do {                                                                                           \
        if (auto result_ = (expression); !result_) {                                               \
            return tl::unexpected(std::move(result_).error());                                     \
        }                                                                                          \
    } while (false)

// Assigns the value of an Expected expression or propagates its error.
// NOLINTBEGIN(bugprone-macro-parentheses): `variable` is a declared name.
#define FEM_ASSIGN(variable, expression)                                                           \
    auto variable##Result_ = (expression);                                                         \
    if (!variable##Result_) {                                                                      \
        return tl::unexpected(std::move(variable##Result_).error());                               \
    }                                                                                              \
    const auto variable = *variable##Result_
// NOLINTEND(bugprone-macro-parentheses)

Expected<GmshReadResult> GmshParser::run()
{
    while (true) {
        m_cursor.skipWhitespace();
        if (m_cursor.atEnd()) {
            break;
        }
        FEM_ASSIGN(header, m_cursor.line());
        if (header.empty() || header.front() != '$') {
            return fail("expected a section such as $Nodes, found '" + std::string(header) + "'");
        }
        const std::string_view name = header.substr(1);
        if (!m_sawFormat && name != "MeshFormat") {
            return fail("the file does not start with $MeshFormat; it is not a Gmsh .msh file");
        }

        if (name == "MeshFormat") {
            FEM_TRY(readMeshFormat());
        } else if (name == "PhysicalNames") {
            FEM_TRY(readPhysicalNames());
        } else if (name == "Entities") {
            FEM_TRY(readEntities());
        } else if (name == "PartitionedEntities") {
            return fail("partitioned meshes are not supported; save the mesh without partitions");
        } else if (name == "Nodes") {
            FEM_TRY(readNodes());
        } else if (name == "Elements") {
            FEM_TRY(readElements());
        } else {
            // REQ-003-18: sections that do not describe the mesh.
            FEM_TRY(m_cursor.skipSection(name));
            continue;
        }
        FEM_TRY(m_cursor.expectEnd(name));
    }

    if (!m_sawFormat) {
        return tl::unexpected(m_cursor.error("the file is empty"));
    }
    if (m_supportedElements == 0) {
        return tl::unexpected(m_cursor.error("the file has no elements of a supported type"));
    }
    FEM_TRY(addGroups());

    auto mesh = m_builder.build();
    if (!mesh) {
        return tl::unexpected(m_cursor.error(mesh.error().message));
    }

    GmshReadResult result{.mesh = std::move(*mesh), .warnings = {}};
    for (const auto& [gmshType, count] : m_ignored) {
        result.warnings.push_back("Ignored " + std::to_string(count) +
                                  " elements of unsupported Gmsh element type " +
                                  std::to_string(gmshType) + ".");
    }
    return result;
}

Expected<void> GmshParser::readMeshFormat()
{
    m_cursor.setBinary(false);
    FEM_ASSIGN(version, m_cursor.token());
    if (version != kSupportedVersion) {
        return fail("Gmsh format version " + std::string(version) +
                    " is not supported; save the mesh as version 4.1");
    }
    FEM_ASSIGN(fileType, m_cursor.readInt());
    FEM_ASSIGN(dataSize, m_cursor.readInt());
    if (fileType != 0 && fileType != 1) {
        return fail("unknown file type " + std::to_string(fileType));
    }
    if (dataSize != kSizeOfSizeT) {
        return fail("files written with " + std::to_string(dataSize) +
                    "-byte sizes are not supported");
    }
    m_binary = fileType == 1;
    if (m_binary) {
        // The line break, then the integer 1 in the writer's byte order.
        FEM_ASSIGN(rest, m_cursor.line());
        if (!rest.empty()) {
            return fail("unexpected text after the format line");
        }
        m_cursor.setBinary(true);
        FEM_ASSIGN(one, m_cursor.readInt());
        if (one != kEndiannessCheck) {
            return fail("the file was written on a computer with a different byte order");
        }
    }
    m_sawFormat = true;
    return {};
}

Expected<void> GmshParser::readPhysicalNames()
{
    // Always ASCII, also in binary files.
    m_cursor.setBinary(false);
    FEM_ASSIGN(count, m_cursor.readSize());
    for (std::uint64_t i = 0; i < count; ++i) {
        FEM_ASSIGN(dimension, m_cursor.readInt());
        FEM_ASSIGN(tag, m_cursor.readInt());
        FEM_ASSIGN(rest, m_cursor.line());
        const std::size_t open = rest.find('"');
        const std::size_t close = rest.rfind('"');
        if (open == std::string_view::npos || close == open) {
            return fail("physical name without quotes");
        }
        m_physicalNames[{dimension, tag}] = std::string(rest.substr(open + 1, close - open - 1));
    }
    return {};
}

Expected<void> GmshParser::readEntities()
{
    m_cursor.setBinary(m_binary);
    std::array<std::uint64_t, 4> counts{};
    for (std::uint64_t& count : counts) {
        FEM_ASSIGN(value, m_cursor.readSize());
        count = value;
    }
    for (int dimension = 0; dimension < 4; ++dimension) {
        for (std::uint64_t i = 0; i < counts.at(static_cast<std::size_t>(dimension)); ++i) {
            FEM_ASSIGN(tag, m_cursor.readInt());
            // Points have a position; the others a bounding box.
            FEM_TRY(m_cursor.skipDoubles(dimension == 0 ? 3 : 6));
            FEM_ASSIGN(physicalCount, m_cursor.readSize());
            std::vector<int>& physicals = m_entityPhysicals[{dimension, tag}];
            for (std::uint64_t p = 0; p < physicalCount; ++p) {
                FEM_ASSIGN(physical, m_cursor.readInt());
                // Gmsh writes a negative tag for a group with reversed orientation.
                physicals.push_back(physical < 0 ? -physical : physical);
            }
            if (dimension > 0) {
                FEM_ASSIGN(boundingCount, m_cursor.readSize());
                for (std::uint64_t b = 0; b < boundingCount; ++b) {
                    FEM_TRY(m_cursor.readInt());
                }
            }
        }
    }
    return {};
}

Expected<void> GmshParser::readNodes()
{
    m_cursor.setBinary(m_binary);
    FEM_ASSIGN(blockCount, m_cursor.readSize());
    FEM_ASSIGN(nodeCount, m_cursor.readSize());
    FEM_TRY(m_cursor.readSize()); // min tag
    FEM_TRY(m_cursor.readSize()); // max tag
    m_builder.reserveNodes(static_cast<std::size_t>(nodeCount));

    std::vector<core::ExternalId> tags;
    for (std::uint64_t block = 0; block < blockCount; ++block) {
        FEM_ASSIGN(entityDimension, m_cursor.readInt());
        FEM_TRY(m_cursor.readInt()); // entity tag
        FEM_ASSIGN(parametric, m_cursor.readInt());
        FEM_ASSIGN(count, m_cursor.readSize());

        tags.clear();
        for (std::uint64_t i = 0; i < count; ++i) {
            FEM_ASSIGN(tag, m_cursor.readSize());
            tags.push_back(static_cast<core::ExternalId>(tag));
        }
        // REQ-003-17: parametric coordinates (u, v, w up to the entity
        // dimension) follow x, y, z and are ignored.
        const int extra = parametric != 0 ? entityDimension : 0;
        for (const core::ExternalId tag : tags) {
            FEM_ASSIGN(x, m_cursor.readDouble());
            FEM_ASSIGN(y, m_cursor.readDouble());
            FEM_ASSIGN(z, m_cursor.readDouble());
            FEM_TRY(m_cursor.skipDoubles(static_cast<std::uint64_t>(extra)));
            if (auto added = m_builder.addNode(tag, Eigen::Vector3d(x, y, z)); !added) {
                return fail(added.error().message);
            }
        }
    }
    if (auto finished = m_builder.finishNodes(); !finished) {
        return fail(finished.error().message);
    }
    return {};
}

Expected<void> GmshParser::readElements()
{
    m_cursor.setBinary(m_binary);
    FEM_ASSIGN(blockCount, m_cursor.readSize());
    FEM_TRY(m_cursor.readSize()); // element count
    FEM_TRY(m_cursor.readSize()); // min tag
    FEM_TRY(m_cursor.readSize()); // max tag

    std::vector<core::ExternalId> nodes;
    for (std::uint64_t block = 0; block < blockCount; ++block) {
        FEM_ASSIGN(entityDimension, m_cursor.readInt());
        FEM_ASSIGN(entityTag, m_cursor.readInt());
        FEM_ASSIGN(gmshType, m_cursor.readInt());
        FEM_ASSIGN(count, m_cursor.readSize());

        const std::optional<ElementType> type = supportedType(gmshType);
        if (!type) {
            // REQ-003-19: skip the block, but only if its size is known.
            const auto& unsupported = unsupportedNodeCounts();
            const auto it = unsupported.find(gmshType);
            if (it == unsupported.end()) {
                return fail("unknown Gmsh element type " + std::to_string(gmshType));
            }
            FEM_TRY(m_cursor.skipSizes(count * static_cast<std::uint64_t>(1 + it->second)));
            m_ignored[gmshType] += count;
            continue;
        }

        const int nodeCount = core::elementTypeInfo(*type).nodeCount;
        m_builder.reserveElements(*type, static_cast<std::size_t>(count));
        std::vector<ElementHandle>& handles = m_entityElements[{entityDimension, entityTag}];
        for (std::uint64_t i = 0; i < count; ++i) {
            FEM_ASSIGN(tag, m_cursor.readSize());
            nodes.clear();
            for (int n = 0; n < nodeCount; ++n) {
                FEM_ASSIGN(node, m_cursor.readSize());
                nodes.push_back(static_cast<core::ExternalId>(node));
            }
            auto handle = m_builder.addElement(*type, static_cast<core::ExternalId>(tag), nodes);
            if (!handle) {
                return fail(handle.error().message);
            }
            handles.push_back(*handle);
        }
        m_supportedElements += count;
    }
    return {};
}

Expected<void> GmshParser::addGroups()
{
    // REQ-003-16: one group per physical group, ordered by dimension and tag.
    std::map<EntityKey, std::vector<ElementHandle>> groups;
    for (const auto& [entity, handles] : m_entityElements) {
        const auto physicals = m_entityPhysicals.find(entity);
        if (physicals == m_entityPhysicals.end()) {
            continue;
        }
        for (const int physical : physicals->second) {
            std::vector<ElementHandle>& members = groups[{entity.first, physical}];
            members.insert(members.end(), handles.begin(), handles.end());
        }
    }
    for (auto& [key, members] : groups) {
        const auto named = m_physicalNames.find(key);
        if (auto added = m_builder.addGroup(named != m_physicalNames.end()
                                                ? named->second
                                                : "group_" + std::to_string(key.first) + "d_" +
                                                      std::to_string(key.second),
                                            key.first, std::move(members));
            !added) {
            return tl::unexpected(m_cursor.error(added.error().message));
        }
    }
    return {};
}

#undef FEM_ASSIGN
#undef FEM_TRY

} // namespace

Expected<GmshReadResult> readGmsh(std::string_view contents, std::string_view sourceName)
{
    GmshParser parser(contents, sourceName);
    return parser.run();
}

Expected<GmshReadResult> readGmsh(const std::filesystem::path& path)
{
    const std::string name = pathToString(path);
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return core::makeError("Could not open the file " + name + ".");
    }
    const std::string contents((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (file.bad()) {
        return core::makeError("Could not read the file " + name + ".");
    }
    return readGmsh(contents, name);
}

} // namespace fem::io
