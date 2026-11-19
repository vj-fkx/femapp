// Measures the beam analysis (NFR-008-02). Not part of ctest: run it on a
// Release build.
//
// Usage: bench_frame [bays] [storeys] [--cost] [--second-order] [--buckling n]
//                    [--warping]
//   A space frame building of (bays + 1)^2 columns and `storeys` storeys
//   (default 10 x 10 bays and 40 storeys: 4 961 nodes, about 29 000 free
//   dofs): columns between the levels, beams along x and y at each level,
//   the base fixed, self-weight and a wind load at the top.
//   --cost (design 008c §5.7): also assembles every bar with the exact
//   engine and factorizes with the indefinite LDL^T, to estimate the second
//   order and the buckling before they exist.
//   --second-order, --buckling n (NFR-008c-02): the second-order analysis
//   and the n first buckling factors of the same load case, against the
//   linear analysis.
//   --warping (NFR-008d-02): every bar has torsion with warping, and the
//   columns are open I sections instead of tubes (Vlasov's warping is for
//   open sections; a closed tube barely warps), as in commit c19d42b.

#include <fem/core/beam_state.hpp>
#include <fem/core/dof_map.hpp>
#include <fem/core/linear_solver.hpp>
#include <fem/core/linear_static.hpp>
#include <fem/core/mesh_builder.hpp>
#include <fem/core/model.hpp>
#include <fem/core/section.hpp>
#include <fem/core/stability_analysis.hpp>
#include <fem/core/static_analysis.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using fem::core::ExternalId;

// The index of an id the model is known to have.
template <typename T>
T found(const std::optional<T>& index)
{
    if (!index) {
        throw std::runtime_error("an id of the frame is missing");
    }
    return *index;
}

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

// The direct solver with the indefinite factorization, timed.
class TimedIndefinite final : public fem::core::LinearSolver
{
public:
    [[nodiscard]] fem::core::Expected<void> factorize(const fem::core::SparseMatrix& m) override
    {
        const auto start = Clock::now();
        auto result = m_solver.factorizeIndefinite(m);
        seconds += secondsSince(start);
        return result;
    }
    [[nodiscard]] fem::core::Expected<Eigen::VectorXd>
    solve(const Eigen::VectorXd& rhs) const override
    {
        return m_solver.solve(rhs);
    }
    double seconds = 0.0;

private:
    fem::core::SparseCholesky m_solver;
};

// T03 of 008c: every bar through the exact engine (with its self-weight),
// assembled, factorized with the indefinite LDL^T and solved, twice (the
// second time with the symbolic analysis kept).
int cost(const fem::core::Model& model, double linear)
{
    const fem::core::Mesh& mesh = *model.mesh;
    const double e = model.materials.front().youngsModulus.value_or(0.0);
    const double g = e / (2.0 * (1.0 + model.materials.front().poissonsRatio.value_or(0.0)));
    const double density = model.materials.front().density.value_or(0.0);
    std::vector<fem::core::BeamData> beams(static_cast<std::size_t>(mesh.elementCount()));
    std::vector<fem::core::SpanLoads> loads(beams.size());
    std::vector<fem::core::ElementIndex> all;
    for (std::size_t a = 0; a < model.assignments.size(); ++a) {
        const auto section = fem::core::sectionProperties(model.sections.at(a));
        if (!section) {
            throw std::runtime_error(section.error().message);
        }
        for (const ExternalId id :
             std::get<fem::core::ElementIds>(model.assignments[a].elements).ids) {
            const fem::core::ElementIndex element = found(mesh.findElement(id));
            const auto nodes = mesh.elementNodes(element);
            const Eigen::Vector3d x1 = mesh.nodeCoordinates(nodes[0]);
            const Eigen::Vector3d x2 = mesh.nodeCoordinates(nodes[1]);
            const auto axes =
                fem::core::beamAxes(x1, x2, std::nullopt, 0.0, fem::core::ElementKind::Beam3D);
            fem::core::BeamData& beam = beams.at(static_cast<std::size_t>(element));
            beam.axial = e * section->area;
            beam.torsional = g * section->torsionConstant;
            beam.bendingY = e * section->inertiaY;
            beam.bendingZ = e * section->inertiaZ;
            beam.shearY = g * section->shearAreaY.value_or(0.0);
            beam.shearZ = g * section->shearAreaZ.value_or(0.0);
            beam.axes = *axes;
            beam.endAxes = *axes;
            // A constant compression of 1 MN: the second order of 008c.
            beam.reference = std::make_shared<const fem::core::ReferenceProfile>(
                fem::core::ReferenceProfile::constant(-1e6));
            const fem::core::SpanFrame frame{.principalAxes = *axes};
            const Eigen::Vector3d weight =
                density * section->area * model.gravity.value_or(Eigen::Vector3d::Zero());
            fem::core::addDistributed(loads.at(static_cast<std::size_t>(element)), frame,
                                      fem::core::Axes::Global, weight, weight, 0.0,
                                      (x2 - x1).norm());
            all.push_back(element);
        }
    }
    const fem::core::ExactBeamElement exact(fem::core::ElementKind::Beam3D, beams, loads);
    const std::array<fem::core::ElementSet, 1> sets = {
        fem::core::ElementSet{.elements = all, .formulation = &exact}};
    fem::core::DofMap dofs = fem::core::DofMap::build(mesh, sets);
    for (const auto& restraint : model.restraints) {
        for (const ExternalId id : std::get<fem::core::NodeIds>(restraint.nodes).ids) {
            if (!dofs.prescribe(found(mesh.findNode(id)), restraint.dof, 0.0)) {
                throw std::runtime_error("cannot prescribe a support");
            }
        }
    }
    std::vector<fem::core::NodalLoad> nodal;
    for (const ExternalId id : std::get<fem::core::NodeIds>(model.nodalForces.front().nodes).ids) {
        nodal.push_back({.node = found(mesh.findNode(id)),
                         .type = fem::core::DofType::U,
                         .value = model.nodalForces.front().force.x()});
    }

    auto start = Clock::now();
    double engine = 0.0;
    for (const fem::core::ElementIndex element : all) {
        const auto nodes = mesh.elementNodes(element);
        const double length =
            (mesh.nodeCoordinates(nodes[1]) - mesh.nodeCoordinates(nodes[0])).norm();
        const auto matrices =
            fem::core::stateBarMatrices(beams.at(static_cast<std::size_t>(element)), length,
                                        loads.at(static_cast<std::size_t>(element)), false);
        engine += matrices.stiffness(0, 0);
    }
    const double sequential = secondsSince(start);

    TimedIndefinite solver;
    std::array<double, 2> rounds = {0.0, 0.0};
    for (double& round : rounds) {
        start = Clock::now();
        const auto solution = fem::core::solveLinear(mesh, dofs, sets, nodal, solver);
        round = secondsSince(start);
        if (!solution) {
            throw std::runtime_error(solution.error().message);
        }
    }
    std::printf("cost (008c T03): exact engine on %zu bars, sequential %.3f s (check %.3g)\n",
                all.size(), sequential, engine);
    std::printf("  exact assembly + indefinite LDL^T + solve: first %.3f s, second %.3f s "
                "(factorizations %.3f s in total)\n",
                rounds[0], rounds[1], solver.seconds);
    // The cost of one more solution with the factorization at hand.
    start = Clock::now();
    const Eigen::VectorXd rhs = Eigen::VectorXd::Ones(dofs.freeCount());
    for (int i = 0; i < 10; ++i) {
        if (!solver.solve(rhs)) {
            throw std::runtime_error("solve failed");
        }
    }
    std::printf("  one solution with the factorization: %.4f s\n", secondsSince(start) / 10);
    const double step = rounds[1];
    std::printf("  estimate: second order with 5 iterations %.3f s = %.2f x linear; "
                "buckling with 15 factorizations %.3f s = %.2f x linear\n",
                linear + 5 * step, (linear + 5 * step) / linear, linear + 15 * step,
                (linear + 15 * step) / linear);
    return EXIT_SUCCESS;
}

int run(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);
    const bool costs = std::erase(args, std::string("--cost")) > 0;
    const bool second = std::erase(args, std::string("--second-order")) > 0;
    const bool warping = std::erase(args, std::string("--warping")) > 0;
    int buckling = 0;
    if (const auto at = std::ranges::find(args, std::string("--buckling")); at != args.end()) {
        if (at + 1 == args.end()) {
            std::fputs("usage: --buckling n\n", stderr);
            return EXIT_FAILURE;
        }
        buckling = std::stoi(*(at + 1));
        args.erase(at, at + 2);
    }
    const int bays = args.size() > 1 ? std::stoi(args[1]) : 10;
    const int storeys = args.size() > 2 ? std::stoi(args[2]) : 40;
    if (bays < 1 || storeys < 1) {
        std::fputs("usage: bench_frame [bays, at least 1] [storeys, at least 1]\n", stderr);
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
    std::printf("analysis, %d free dofs%s: %.3f s (validation, assembly, solution, "
                "reactions and end forces); largest displacement %.4g\n",
                6 * side * side * storeys, warping ? " plus the warping ones" : "", total, largest);
    if (second) {
        start = Clock::now();
        const auto r = fem::core::runSecondOrderStatic(model);
        const double seconds = secondsSince(start);
        if (!r) {
            std::fprintf(stderr, "%s\n", r.error().message.c_str());
            return EXIT_FAILURE;
        }
        double sway = 0.0;
        for (const auto& u : r->result.displacements) {
            sway = std::max(sway, u.norm());
        }
        std::printf("second order: %.3f s = %.2f x linear, %d iterations (change %.2g); largest "
                    "displacement %.4g (%.3f x linear)\n",
                    seconds, seconds / total, r->iterations, r->change, sway, sway / largest);
    }
    if (buckling > 0) {
        start = Clock::now();
        const auto r = fem::core::runLinearBuckling(model, {.count = buckling});
        const double seconds = secondsSince(start);
        if (!r) {
            std::fprintf(stderr, "%s\n", r.error().message.c_str());
            return EXIT_FAILURE;
        }
        std::printf("buckling, %d factors: %.3f s = %.2f x linear; factors", buckling, seconds,
                    seconds / total);
        for (const auto& mode : r->modes) {
            std::printf(" %.6g", mode.factor);
        }
        std::printf("%s%s\n", r->note.empty() ? "" : "; ", r->note.c_str());
    }
    if (costs) {
        return cost(model, total);
    }
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
