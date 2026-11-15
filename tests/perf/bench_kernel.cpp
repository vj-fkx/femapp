// Measures the FEM kernel (NFR-006-01 to 03). Not part of ctest: run it on a
// Release build.
//
// Usage: bench_kernel <tet4 mesh.msh> [cells per side of the solve cube]
//   - assembles the test Poisson element on the mesh with 1 thread and with
//     all of them (NFR-006-01, 02);
//   - solves Poisson on an n^3 hexahedron cube (default n = 30, about 25 000
//     dofs) with the direct solver (NFR-006-03).

#include "poisson_element.hpp"

#include <fem/core/assembler.hpp>
#include <fem/core/dof_map.hpp>
#include <fem/core/linear_solver.hpp>
#include <fem/core/linear_static.hpp>
#include <fem/core/mesh_builder.hpp>
#include <fem/io/gmsh_reader.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double source(const Eigen::Vector3d& /*x*/)
{
    return 1.0;
}

// n^3 unit hexahedra.
fem::core::Mesh cube(int n)
{
    fem::core::MeshBuilder builder;
    const auto id = [n](int i, int j, int k) {
        const fem::core::ExternalId side = fem::core::ExternalId{n} + 1;
        return 1 + i + side * (j + side * k);
    };
    for (int k = 0; k <= n; ++k) {
        for (int j = 0; j <= n; ++j) {
            for (int i = 0; i <= n; ++i) {
                (void)builder.addNode(id(i, j, k), Eigen::Vector3d(i, j, k) / n);
            }
        }
    }
    fem::core::ExternalId element = 1;
    for (int k = 0; k < n; ++k) {
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                const std::vector<fem::core::ExternalId> nodes = {id(i, j, k),
                                                                  id(i + 1, j, k),
                                                                  id(i + 1, j + 1, k),
                                                                  id(i, j + 1, k),
                                                                  id(i, j, k + 1),
                                                                  id(i + 1, j, k + 1),
                                                                  id(i + 1, j + 1, k + 1),
                                                                  id(i, j + 1, k + 1)};
                (void)builder.addElement(fem::core::ElementType::Hex8, element++, nodes);
            }
        }
    }
    auto mesh = builder.build();
    if (!mesh) {
        throw std::runtime_error(mesh.error().message);
    }
    return std::move(*mesh);
}

std::vector<fem::core::ElementIndex> allElements(const fem::core::Mesh& mesh)
{
    std::vector<fem::core::ElementIndex> elements(static_cast<std::size_t>(mesh.elementCount()));
    std::iota(elements.begin(), elements.end(), fem::core::ElementIndex{0});
    return elements;
}

int run(int argc, char** argv)
{
    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    if (args.size() < 2) {
        std::fputs("usage: bench_kernel <tet4 mesh.msh> [n]\n", stderr);
        return EXIT_FAILURE;
    }
    const int n = args.size() > 2 ? std::stoi(args[2]) : 30;
    const fem::core::test::PoissonElement element(source);

    // NFR-006-01, 02: assembly on the large mesh.
    auto read = fem::io::readGmsh(std::filesystem::path(args[1]));
    if (!read) {
        std::fprintf(stderr, "%s\n", read.error().message.c_str());
        return EXIT_FAILURE;
    }
    const fem::core::Mesh& mesh = read->mesh;
    const auto elements = allElements(mesh);
    const std::vector<fem::core::ElementSet> sets = {
        {.elements = elements, .formulation = &element}};
    auto start = Clock::now();
    const fem::core::DofMap dofs = fem::core::DofMap::build(mesh, sets);
    std::printf("%d elements, %d dofs; DofMap %.3f s\n", mesh.elementCount(), dofs.size(),
                secondsSince(start));
    std::array<double, 2> times{};
    const std::array<int, 2> threadCounts = {1, 0};
    for (int t = 0; t < 2; ++t) {
        start = Clock::now();
        const auto system =
            fem::core::assemble(mesh, dofs, sets, {}, threadCounts.at(static_cast<std::size_t>(t)));
        times.at(static_cast<std::size_t>(t)) = secondsSince(start);
        if (!system) {
            std::fprintf(stderr, "%s\n", system.error().message.c_str());
            return EXIT_FAILURE;
        }
        std::printf("assembly, %s: %.3f s (%lld nonzeros)\n", t == 0 ? "1 thread" : "all threads",
                    times.at(static_cast<std::size_t>(t)),
                    static_cast<long long>(system->matrix.nonZeros()));
    }
    std::printf("speed-up with %u hardware threads: %.2f\n", std::thread::hardware_concurrency(),
                times[0] / times[1]);

    // NFR-006-03: direct solution of about 100 000 dofs.
    const fem::core::Mesh box = cube(n);
    const auto boxElements = allElements(box);
    const std::vector<fem::core::ElementSet> boxSets = {
        {.elements = boxElements, .formulation = &element}};
    fem::core::DofMap boxDofs = fem::core::DofMap::build(box, boxSets);
    for (fem::core::NodeIndex node = 0; node < box.nodeCount(); ++node) {
        const Eigen::Vector3d x = box.nodeCoordinates(node);
        if (x.minCoeff() < 1e-12 || x.maxCoeff() > 1.0 - 1e-12) {
            (void)boxDofs.prescribe(node, fem::core::DofType::T, 0.0);
        }
    }
    fem::core::SparseCholesky solver;
    start = Clock::now();
    const auto solution = fem::core::solveLinear(box, boxDofs, boxSets, {}, solver);
    if (!solution) {
        std::fprintf(stderr, "%s\n", solution.error().message.c_str());
        return EXIT_FAILURE;
    }
    std::printf("solve, %d^3 Hex8, %d free dofs: %.3f s (assembly, factorization, "
                "solution and reactions)\n",
                n, boxDofs.freeCount(), secondsSince(start));
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
