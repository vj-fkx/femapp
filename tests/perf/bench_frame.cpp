// Measures the beam analysis (NFR-008-02). Not part of ctest: run it on a
// Release build.
//
// Usage: bench_frame [bays] [storeys] [warping]
//   A space frame building of (bays + 1)^2 columns and `storeys` storeys
//   (default 10 x 10 bays and 40 storeys: 4 961 nodes, about 29 000 free
//   dofs): columns between the levels, beams along x and y at each level,
//   the base fixed, self-weight and a wind load at the top. With a third
//   argument "warping" (NFR-008b-03), every bar has torsion with warping
//   (I columns, so that they warp too).

#include <fem/core/mesh_builder.hpp>
#include <fem/core/model.hpp>
#include <fem/core/static_analysis.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using fem::core::ExternalId;

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

fem::core::Model building(int bays, int storeys, bool warping)
{
    const double bay = 6.0;
    const double storey = 3.5;
    const int side = bays + 1;
    const auto id = [side](int i, int j, int k) {
        return 1 + i + ExternalId{side} * (j + ExternalId{side} * k);
    };
    fem::core::MeshBuilder builder;
    for (int k = 0; k <= storeys; ++k) {
        for (int j = 0; j < side; ++j) {
            for (int i = 0; i < side; ++i) {
                (void)builder.addNode(id(i, j, k), Eigen::Vector3d(i * bay, j * bay, k * storey));
            }
        }
    }
    ExternalId element = 1;
    std::vector<ExternalId> columns;
    std::vector<ExternalId> beams;
    const auto add = [&](ExternalId a, ExternalId b, std::vector<ExternalId>& list) {
        const std::array<ExternalId, 2> nodes = {a, b};
        (void)builder.addElement(fem::core::ElementType::Line2, element, nodes);
        list.push_back(element++);
    };
    for (int k = 1; k <= storeys; ++k) {
        for (int j = 0; j < side; ++j) {
            for (int i = 0; i < side; ++i) {
                add(id(i, j, k - 1), id(i, j, k), columns);
                if (i + 1 < side) {
                    add(id(i, j, k), id(i + 1, j, k), beams);
                }
                if (j + 1 < side) {
                    add(id(i, j, k), id(i, j + 1, k), beams);
                }
            }
        }
    }
    auto mesh = builder.build();
    if (!mesh) {
        throw std::runtime_error(mesh.error().message);
    }

    fem::core::Model model;
    model.mesh = std::make_shared<const fem::core::Mesh>(std::move(*mesh));
    model.materials.push_back({.name = "Steel",
                               .youngsModulus = 2.1e11,
                               .poissonsRatio = 0.3,
                               .density = 7850.0,
                               .thermalExpansion = 1.2e-5,
                               .conductivity = {},
                               .specificHeat = {}});
    if (warping) {
        // Open columns: Vlasov's warping is for open sections (a closed tube
        // barely warps, and its k L in the hundreds only costs subdivisions).
        model.sections.push_back(
            {.name = "Column",
             .shape = fem::core::ISection{.h = 0.4, .bf = 0.3, .tf = 0.024, .tw = 0.0135}});
    } else {
        model.sections.push_back(
            {.name = "Column", .shape = fem::core::RectangularTube{.b = 0.4, .h = 0.4, .t = 0.02}});
    }
    model.sections.push_back(
        {.name = "Beam",
         .shape = fem::core::ISection{.h = 0.5, .bf = 0.2, .tf = 0.016, .tw = 0.01}});
    model.assignments.push_back({.elements = fem::core::ElementIds{columns},
                                 .kind = fem::core::ElementKind::Beam3D,
                                 .material = "Steel",
                                 .section = "Column",
                                 .warping = warping});
    model.assignments.push_back({.elements = fem::core::ElementIds{beams},
                                 .kind = fem::core::ElementKind::Beam3D,
                                 .material = "Steel",
                                 .section = "Beam",
                                 .warping = warping});
    fem::core::NodeIds base;
    fem::core::NodeIds top;
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            base.ids.push_back(id(i, j, 0));
            top.ids.push_back(id(i, j, storeys));
        }
    }
    for (const auto dof :
         {fem::core::DofType::U, fem::core::DofType::V, fem::core::DofType::W,
          fem::core::DofType::RX, fem::core::DofType::RY, fem::core::DofType::RZ}) {
        model.restraints.push_back({.nodes = base, .dof = dof, .value = 0.0});
    }
    model.nodalForces.push_back({.nodes = top, .force = Eigen::Vector3d(2.0e4, 1.0e4, 0.0)});
    model.gravity = Eigen::Vector3d(0.0, 0.0, -9.81);
    return model;
}

int run(int argc, char** argv)
{
    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    const int bays = args.size() > 1 ? std::stoi(args[1]) : 10;
    const int storeys = args.size() > 2 ? std::stoi(args[2]) : 40;
    const bool warping = args.size() > 3 && std::string(args[3]) == "warping";
    if (bays < 1 || storeys < 1) {
        std::fputs("usage: bench_frame [bays, at least 1] [storeys, at least 1] [warping]\n",
                   stderr);
        return EXIT_FAILURE;
    }
    auto start = Clock::now();
    const fem::core::Model model = building(bays, storeys, warping);
    std::printf("%d nodes, %d bars; model built in %.3f s\n", model.mesh->nodeCount(),
                model.mesh->elementCount(), secondsSince(start));

    start = Clock::now();
    const auto issues = fem::core::validate(model);
    std::printf("validation: %.3f s (%zu issues)\n", secondsSince(start), issues.size());

    start = Clock::now();
    const auto result = fem::core::runLinearStatic(model);
    const double total = secondsSince(start);
    if (!result) {
        std::fprintf(stderr, "%s\n", result.error().message.c_str());
        return EXIT_FAILURE;
    }
    double largest = 0.0;
    for (const auto& u : result->displacements) {
        largest = std::max(largest, u.norm());
    }
    const int side = bays + 1;
    std::printf("analysis, %s%d free dofs: %.3f s (validation, assembly, solution, "
                "reactions and end forces); largest displacement %.4g\n",
                warping ? "with warping, about " : "", (warping ? 7 : 6) * side * side * storeys,
                total, largest);
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
