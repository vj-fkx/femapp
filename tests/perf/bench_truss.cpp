// Measures the truss analysis (NFR-007-02). Not part of ctest: run it on a
// Release build.
//
// Usage: bench_truss [nodes per side]
//   A space truss of n^3 nodes (default n = 18, about 16 500 dofs): bars along
//   x, y, z, the face diagonals (1,1,0), (1,0,1), (0,1,1) and the body
//   diagonal (1,1,1) from each node, which are the edges of a Kuhn
//   tetrahedral grid and make the truss rigid. Bottom nodes held, self-weight
//   and a load on the top nodes.

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

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

fem::core::Model lattice(int n)
{
    const double spacing = 1000.0;
    const auto id = [n](int i, int j, int k) {
        return 1 + i + fem::core::ExternalId{n} * (j + fem::core::ExternalId{n} * k);
    };
    fem::core::MeshBuilder builder;
    for (int k = 0; k < n; ++k) {
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                (void)builder.addNode(id(i, j, k), Eigen::Vector3d(i, j, k) * spacing);
            }
        }
    }
    constexpr std::array<std::array<int, 3>, 7> kDirections = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}}};
    fem::core::ExternalId bar = 1;
    std::vector<fem::core::ExternalId> bars;
    for (int k = 0; k < n; ++k) {
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                for (const auto& d : kDirections) {
                    if (i + d[0] >= n || j + d[1] >= n || k + d[2] >= n) {
                        continue;
                    }
                    const std::array<fem::core::ExternalId, 2> nodes = {
                        id(i, j, k), id(i + d[0], j + d[1], k + d[2])};
                    (void)builder.addElement(fem::core::ElementType::Line2, bar, nodes);
                    bars.push_back(bar++);
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
                               .youngsModulus = 210e3,
                               .poissonsRatio = 0.3,
                               .density = 7.85e-9,
                               .thermalExpansion = 1.2e-5,
                               .conductivity = {},
                               .specificHeat = {}});
    model.sections.push_back({.name = "Tube", .area = 800.0});
    model.assignments.push_back({.elements = fem::core::ElementIds{bars},
                                 .kind = fem::core::ElementKind::Truss3D,
                                 .material = "Steel",
                                 .section = "Tube"});
    fem::core::NodeIds bottom;
    fem::core::NodeIds top;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            bottom.ids.push_back(id(i, j, 0));
            top.ids.push_back(id(i, j, n - 1));
        }
    }
    for (const auto dof : {fem::core::DofType::U, fem::core::DofType::V, fem::core::DofType::W}) {
        model.restraints.push_back({.nodes = bottom, .dof = dof, .value = 0.0});
    }
    model.nodalForces.push_back({.nodes = top, .force = Eigen::Vector3d(500.0, 0.0, -2000.0)});
    model.gravity = Eigen::Vector3d(0.0, 0.0, -9810.0);
    return model;
}

int run(int argc, char** argv)
{
    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    const int n = args.size() > 1 ? std::stoi(args[1]) : 18;
    if (n < 2) {
        std::fputs("usage: bench_truss [nodes per side, at least 2]\n", stderr);
        return EXIT_FAILURE;
    }
    auto start = Clock::now();
    const fem::core::Model model = lattice(n);
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
    std::printf("analysis, %d free dofs: %.3f s (validation, assembly, solution, "
                "reactions and bar forces); largest displacement %.4g\n",
                3 * n * n * (n - 1), total, largest);
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
