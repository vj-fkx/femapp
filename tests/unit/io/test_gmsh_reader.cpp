#include <fem/core/element_type.hpp>
#include <fem/core/mesh_measure.hpp>
#include <fem/io/gmsh_reader.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using fem::core::ElementFamily;
using fem::core::ElementType;
using fem::core::Mesh;
using fem::io::GmshReadResult;

namespace {

// Value of an optional that the test requires to be present. Written without
// operator* so that static analysis sees the check.
template <typename T>
T required(const std::optional<T>& value)
{
    REQUIRE(value.has_value());
    return value.value_or(T{});
}

const std::filesystem::path kDataDir = std::filesystem::path(FEMAPP_TEST_DATA_DIR) / "gmsh";

GmshReadResult readOk(const std::string& name)
{
    auto result = fem::io::readGmsh(kDataDir / name);
    if (!result) {
        FAIL(result.error().message);
    }
    return std::move(*result);
}

std::string readError(const std::string& name)
{
    const auto result = fem::io::readGmsh(kDataDir / name);
    REQUIRE_FALSE(result.has_value());
    return result.error().message;
}

std::string fileContents(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Must match all_types_transform() in tests/data/gmsh/generate_test_meshes.py.
Eigen::Vector3d allTypesTransform(int gmshType, const std::array<double, 3>& reference)
{
    const double angle = 0.3 + 0.05 * gmshType;
    const Eigen::Vector3d axis = Eigen::Vector3d(1, 2, 3).normalized();
    const double scale = 1.5 + 0.1 * (gmshType % 7);
    const Eigen::Vector3d shift(20.0 * gmshType, -3.0 * gmshType, 1.5 * gmshType);
    const Eigen::Vector3d p(reference[0], reference[1], reference[2]);
    return Eigen::AngleAxisd(angle, axis) * (scale * p) + shift;
}

double referenceMeasure(ElementFamily family)
{
    switch (family) {
    case ElementFamily::Point:
        return 1.0;
    case ElementFamily::Line:
        return 2.0;
    case ElementFamily::Triangle:
        return 0.5;
    case ElementFamily::Quadrilateral:
        return 4.0;
    case ElementFamily::Tetrahedron:
        return 1.0 / 6.0;
    case ElementFamily::Hexahedron:
        return 8.0;
    case ElementFamily::Wedge:
        return 1.0;
    case ElementFamily::Pyramid:
        return 4.0 / 3.0;
    }
    return 0.0;
}

// Nodes, elements and groups compared by their external ids.
void checkSameMesh(const Mesh& a, const Mesh& b)
{
    REQUIRE(a.nodeCount() == b.nodeCount());
    REQUIRE(a.elementCount() == b.elementCount());
    for (fem::core::NodeIndex n = 0; n < a.nodeCount(); ++n) {
        REQUIRE(a.nodeId(n) == b.nodeId(n));
        // Gmsh writes ASCII coordinates with 16 significant digits, one short
        // of an exact round trip, so the two files may differ in the last bit.
        const Eigen::Vector3d pa = a.nodeCoordinates(n);
        const Eigen::Vector3d pb = b.nodeCoordinates(n);
        REQUIRE((pa - pb).norm() <= 1e-15 * std::max(1.0, pa.norm()));
    }
    for (fem::core::ElementIndex e = 0; e < a.elementCount(); ++e) {
        REQUIRE(a.elementId(e) == b.elementId(e));
        REQUIRE(a.elementType(e) == b.elementType(e));
        const auto nodesA = a.elementNodes(e);
        const auto nodesB = b.elementNodes(e);
        REQUIRE(std::ranges::equal(nodesA, nodesB));
    }
    REQUIRE(a.groups().size() == b.groups().size());
    for (std::size_t g = 0; g < a.groups().size(); ++g) {
        CHECK(a.groups()[g].name == b.groups()[g].name);
        CHECK(a.groups()[g].dimension == b.groups()[g].dimension);
        CHECK(a.groups()[g].elements == b.groups()[g].elements);
    }
}

// Sum of the measures of the elements of the mesh's own dimension.
double totalMeasure(const Mesh& mesh, bool& allPositive)
{
    double total = 0.0;
    allPositive = true;
    for (fem::core::ElementIndex e = 0; e < mesh.elementCount(); ++e) {
        if (fem::core::elementTypeInfo(mesh.elementType(e)).dimension != mesh.dimension()) {
            continue;
        }
        const double measure = fem::core::elementMeasure(mesh, e);
        allPositive = allPositive && measure > 0.0;
        total += measure;
    }
    return total;
}

} // namespace

// REQ-003-15, REQ-003-03, AC-7: one element of every supported type, written
// by Gmsh, read back with every node where the reference coordinates put it.
TEST_CASE("every supported element type is read with its node order", "[io][gmsh]")
{
    const std::string file = GENERATE("all_types.msh", "all_types_bin.msh");
    INFO(file);
    const GmshReadResult result = readOk(file);
    const Mesh& mesh = result.mesh;
    CHECK(result.warnings.empty());
    REQUIRE(mesh.elementCount() == fem::core::kElementTypeCount);

    for (const ElementType type : fem::core::allElementTypes()) {
        const auto& info = fem::core::elementTypeInfo(type);
        INFO(info.name);
        const fem::core::ElementIndex element = required(mesh.findElement(info.gmshType));
        CHECK(mesh.elementType(element) == type);

        const auto nodes = mesh.elementNodes(element);
        const auto reference = fem::core::referenceNodeCoordinates(type);
        REQUIRE(nodes.size() == reference.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const Eigen::Vector3d expected = allTypesTransform(info.gmshType, reference[i]);
            const Eigen::Vector3d actual = mesh.nodeCoordinates(nodes[i]);
            CHECK((actual - expected).norm() < 1e-9);
        }

        const double scale = 1.5 + 0.1 * (info.gmshType % 7);
        const double expectedMeasure =
            std::pow(scale, info.dimension) * referenceMeasure(info.family);
        CHECK_THAT(fem::core::elementMeasure(mesh, element), WithinRel(expectedMeasure, 1e-9));
    }
}

// REQ-003-15: ASCII and binary files of the same model give the same mesh.
TEST_CASE("ASCII and binary files give identical meshes", "[io][gmsh]")
{
    const std::string stem = GENERATE("all_types", "segment_o2", "square_tri6", "square_quad8",
                                      "box_hex20", "box_wedge15", "box_tet10");
    INFO(stem);
    const GmshReadResult ascii = readOk(stem + ".msh");
    const GmshReadResult binary = readOk(stem + "_bin.msh");
    checkSameMesh(ascii.mesh, binary.mesh);
}

// REQ-003-15, REQ-003-08, AC-3: real meshes reproduce the known measure.
TEST_CASE("real meshes reproduce their length, area and volume", "[io][gmsh]")
{
    struct Case
    {
        const char* file;
        ElementType type;
        double measure;
    };
    const Case c =
        GENERATE(Case{.file = "segment_o1", .type = ElementType::Line2, .measure = 2.0},
                 Case{.file = "segment_o2", .type = ElementType::Line3, .measure = 2.0},
                 Case{.file = "square_tri3", .type = ElementType::Tri3, .measure = 6.0},
                 Case{.file = "square_tri6", .type = ElementType::Tri6, .measure = 6.0},
                 Case{.file = "square_quad4", .type = ElementType::Quad4, .measure = 6.0},
                 Case{.file = "square_quad8", .type = ElementType::Quad8, .measure = 6.0},
                 Case{.file = "square_quad9", .type = ElementType::Quad9, .measure = 6.0},
                 Case{.file = "box_hex8", .type = ElementType::Hex8, .measure = 6.0},
                 Case{.file = "box_hex20", .type = ElementType::Hex20, .measure = 6.0},
                 Case{.file = "box_wedge6", .type = ElementType::Wedge6, .measure = 6.0},
                 Case{.file = "box_wedge15", .type = ElementType::Wedge15, .measure = 6.0},
                 Case{.file = "box_tet4", .type = ElementType::Tet4, .measure = 6.0},
                 Case{.file = "box_tet10", .type = ElementType::Tet10, .measure = 6.0});
    const std::string suffix = GENERATE(".msh", "_bin.msh");
    INFO(c.file << suffix);

    const GmshReadResult result = readOk(std::string(c.file) + suffix);
    const Mesh& mesh = result.mesh;
    CHECK(mesh.elementCount(c.type) > 0);
    bool allPositive = false;
    CHECK_THAT(totalMeasure(mesh, allPositive), WithinRel(c.measure, 1e-9));
    CHECK(allPositive);
}

// REQ-003-16: physical groups, with names from the file or generated.
TEST_CASE("physical groups become named groups", "[io][gmsh][group]")
{
    const std::string suffix = GENERATE(".msh", "_bin.msh");
    INFO(suffix);

    const GmshReadResult square = readOk("square_tri3" + suffix);
    const Mesh& mesh = square.mesh;
    const fem::core::Group* plate = mesh.findGroup("plate", 2);
    REQUIRE(plate != nullptr);
    CHECK(static_cast<int>(plate->elements.size()) == mesh.elementCount(ElementType::Tri3));
    const fem::core::Group* left = mesh.findGroup("left", 1);
    REQUIRE(left != nullptr);
    CHECK_FALSE(left->elements.empty());

    // The unnamed 1D group covers the whole boundary, including "left".
    const auto unnamed =
        std::find_if(mesh.groups().begin(), mesh.groups().end(),
                     [](const fem::core::Group& g) { return g.name.starts_with("group_1d_"); });
    REQUIRE(unnamed != mesh.groups().end());
    CHECK(unnamed->dimension == 1);
    CHECK(unnamed->elements.size() > left->elements.size());
    for (const fem::core::ElementIndex e : left->elements) {
        CHECK(std::binary_search(unnamed->elements.begin(), unnamed->elements.end(), e));
    }
    // Total boundary length of the 2 x 3 square.
    double perimeter = 0.0;
    for (const fem::core::ElementIndex e : unnamed->elements) {
        perimeter += fem::core::elementMeasure(mesh, e);
    }
    CHECK_THAT(perimeter, WithinRel(10.0, 1e-9));

    const GmshReadResult box = readOk("box_tet4" + suffix);
    REQUIRE(box.mesh.findGroup("solid", 3) != nullptr);
    CHECK(static_cast<int>(box.mesh.findGroup("solid", 3)->elements.size()) ==
          box.mesh.elementCount(ElementType::Tet4));
}

// REQ-003-17: sparse ids in any order and parametric node coordinates.
TEST_CASE("sparse ids and parametric coordinates are accepted", "[io][gmsh]")
{
    const GmshReadResult result = readOk("handwritten/sparse_parametric.msh");
    const Mesh& mesh = result.mesh;
    REQUIRE(mesh.nodeCount() == 4);
    CHECK(mesh.nodeCoordinates(required(mesh.findNode(40))) == Eigen::Vector3d(0, 0, 0));
    CHECK(mesh.nodeCoordinates(required(mesh.findNode(7))) == Eigen::Vector3d(1, 0, 0));
    CHECK(mesh.nodeCoordinates(required(mesh.findNode(1000))) == Eigen::Vector3d(1, 1, 0));
    CHECK(mesh.nodeCoordinates(required(mesh.findNode(23))) == Eigen::Vector3d(0, 1, 0));
    REQUIRE(mesh.findElement(900).has_value());
    REQUIRE(mesh.findElement(5).has_value());

    const fem::core::Group* patch = mesh.findGroup("patch", 2);
    REQUIRE(patch != nullptr);
    CHECK(patch->elements.size() == 2);
    bool allPositive = false;
    CHECK_THAT(totalMeasure(mesh, allPositive), WithinRel(1.0, 1e-12));
    CHECK(allPositive);
}

// REQ-003-18, REQ-003-19: extra sections skipped; unsupported types ignored
// with a warning naming the type and the count.
TEST_CASE("extra sections and unsupported types are skipped", "[io][gmsh]")
{
    const GmshReadResult result = readOk("handwritten/extra_sections.msh");
    CHECK(result.mesh.elementCount() == 2);
    CHECK(result.mesh.elementCount(ElementType::Line2) == 2);
    REQUIRE(result.warnings.size() == 1);
    CHECK_THAT(result.warnings[0],
               ContainsSubstring("Ignored 2 elements") && ContainsSubstring("type 84"));
}

// REQ-003-20
TEST_CASE("a file with only unsupported types is refused", "[io][gmsh][error]")
{
    CHECK_THAT(readError("handwritten/only_unsupported.msh"),
               ContainsSubstring("no elements of a supported type"));
}

// REQ-003-21
TEST_CASE("other versions and partitioned meshes are refused", "[io][gmsh][error]")
{
    CHECK_THAT(readError("handwritten/version22.msh"),
               ContainsSubstring("version 2.2 is not supported"));
    CHECK_THAT(readError("handwritten/partitioned.msh"),
               ContainsSubstring("partitioned meshes are not supported"));
}

// REQ-003-21: a binary file with the other byte order.
TEST_CASE("a binary file with another byte order is refused", "[io][gmsh][error]")
{
    std::string contents = fileContents(kDataDir / "all_types_bin.msh");
    const std::string header = "$MeshFormat\n4.1 1 8\n";
    REQUIRE(contents.compare(0, header.size(), header) == 0);
    // Byte-swap the endianness check (the int 1 right after the header line).
    std::swap(contents[header.size()], contents[header.size() + 3]);
    std::swap(contents[header.size() + 1], contents[header.size() + 2]);

    const auto result = fem::io::readGmsh(contents, "swapped.msh");
    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error().message, ContainsSubstring("different byte order"));
}

// REQ-003-22, REQ-003-23: malformed files, with the position of the problem.
TEST_CASE("malformed ASCII files are refused with the line", "[io][gmsh][error]")
{
    CHECK_THAT(readError("handwritten/bad_number.msh"),
               ContainsSubstring("line 10") && ContainsSubstring("invalid number '1.0x'"));
    CHECK_THAT(readError("handwritten/missing_node.msh"),
               ContainsSubstring("line 15") && ContainsSubstring("node 99"));
    CHECK_THAT(readError("handwritten/truncated.msh"),
               ContainsSubstring("unexpected end of file") && ContainsSubstring("line"));
}

// REQ-003-22, REQ-003-23
TEST_CASE("malformed binary files are refused with the byte", "[io][gmsh][error]")
{
    const std::string contents = fileContents(kDataDir / "box_tet4_bin.msh");
    const std::size_t nodes = contents.find("$Nodes\n");
    REQUIRE(nodes != std::string::npos);

    // Cut in the middle of the nodes.
    const std::string truncated = contents.substr(0, nodes + 200);
    const auto result = fem::io::readGmsh(truncated, "truncated_bin.msh");
    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error().message, ContainsSubstring("truncated_bin.msh, byte") &&
                                           ContainsSubstring("unexpected end of file"));
}

// REQ-003-22
TEST_CASE("a missing file is reported", "[io][gmsh][error]")
{
    CHECK_THAT(readError("no_such_file.msh"),
               StartsWith("Could not open the file") && ContainsSubstring("no_such_file.msh"));
}

// REQ-003-24: paths with characters outside ASCII.
TEST_CASE("files in paths with accents are read", "[io][gmsh]")
{
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / std::filesystem::path(u8"femapp_ção_测试");
    std::filesystem::create_directories(folder);
    const std::filesystem::path target = folder / std::filesystem::path(u8"malha_ação.msh");
    std::filesystem::copy_file(kDataDir / "segment_o1.msh", target,
                               std::filesystem::copy_options::overwrite_existing);

    const auto result = fem::io::readGmsh(target);
    std::filesystem::remove_all(folder);
    REQUIRE(result.has_value());
    CHECK(result->mesh.elementCount() == 5);
}
