// REQ-007-08, 11: the truss bar on its own: stiffness, rigid body modes and
// equivalent loads.

#include <fem/core/truss_element.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <array>
#include <cmath>
#include <vector>

using fem::core::BarData;
using fem::core::ElementKind;
using fem::core::TrussElement;

namespace {

struct Local
{
    Eigen::MatrixXd matrix;
    Eigen::VectorXd vector;
};

Local computeBar(ElementKind kind, const BarData& bar, const Eigen::Vector3d& a,
                 const Eigen::Vector3d& b)
{
    const std::vector<BarData> bars = {bar};
    const TrussElement element(kind, bars);
    const int size = kind == ElementKind::Truss2D ? 4 : 6;
    Local local{.matrix = Eigen::MatrixXd::Zero(size, size), .vector = Eigen::VectorXd::Zero(size)};
    fem::core::ElementNodes nodes;
    nodes.type = fem::core::ElementType::Line2;
    nodes.count = 2;
    nodes.x[0] = a;
    nodes.x[1] = b;
    REQUIRE(element.compute(nodes, 0, local.matrix, local.vector).has_value());
    return local;
}

// Nodal displacements (u1; u2) as a vector of the element's dofs.
Eigen::VectorXd stacked(int dims, const Eigen::Vector3d& u1, const Eigen::Vector3d& u2)
{
    Eigen::VectorXd u(2 * dims);
    u << u1.head(dims), u2.head(dims);
    return u;
}

double relative(double value, double reference)
{
    return std::abs(value - reference) / std::max(1.0, std::abs(reference));
}

} // namespace

TEST_CASE("truss dofs per node", "[core][truss]")
{
    const std::vector<BarData> bars(1);
    using fem::core::DofType;
    CHECK(TrussElement(ElementKind::Truss2D, bars).dofsAt(fem::core::ElementType::Line2, 0) ==
          (fem::core::mask(DofType::U) | fem::core::mask(DofType::V)));
    CHECK(
        TrussElement(ElementKind::Truss3D, bars).dofsAt(fem::core::ElementType::Line2, 1) ==
        (fem::core::mask(DofType::U) | fem::core::mask(DofType::V) | fem::core::mask(DofType::W)));
}

// REQ-007-08: the closed form in any orientation, rank 1, and zero energy in
// every rigid body motion.
TEST_CASE("truss stiffness", "[core][truss]")
{
    const ElementKind kind = GENERATE(ElementKind::Truss2D, ElementKind::Truss3D);
    const int dims = kind == ElementKind::Truss2D ? 2 : 3;
    const BarData bar{
        .axialStiffness = 2.5e7, .imposedStrain = 0.0, .weight = Eigen::Vector3d::Zero()};
    const Eigen::Vector3d a(0.3, -1.2, 0.0);
    const Eigen::Vector3d b(2.1, 0.4, dims == 3 ? 1.7 : 0.0);
    const Local local = computeBar(kind, bar, a, b);

    const double length = (b - a).norm();
    const Eigen::VectorXd e = ((b - a) / length).head(dims);
    Eigen::VectorXd g(2 * dims);
    g << -e, e;
    const Eigen::MatrixXd expected = bar.axialStiffness / length * g * g.transpose();
    CHECK((local.matrix - expected).norm() <= 1e-12 * expected.norm());
    CHECK(local.vector.norm() == 0.0);

    const Eigen::FullPivLU<Eigen::MatrixXd> lu(local.matrix);
    CHECK(lu.rank() == 1);

    // Translations and (small) rotations about any axis.
    const double scale = bar.axialStiffness / length;
    for (int axis = 0; axis < 3; ++axis) {
        const Eigen::Vector3d t = Eigen::Vector3d::Unit(axis);
        const Eigen::VectorXd u = stacked(dims, t, t);
        CHECK(std::abs(u.dot(local.matrix * u)) <= 1e-12 * scale);
        if (dims == 2 && axis != 2) {
            continue; // in the plane only rotation about z
        }
        const Eigen::VectorXd r = stacked(dims, t.cross(a), t.cross(b));
        CHECK((local.matrix * r).norm() <= 1e-12 * scale * r.norm());
    }
}

// REQ-007-08: rotating the bar rotates its matrix.
TEST_CASE("truss stiffness is invariant under rotation", "[core][truss]")
{
    const BarData bar{
        .axialStiffness = 1.0, .imposedStrain = 0.0, .weight = Eigen::Vector3d::Zero()};
    const Eigen::Vector3d a(0.0, 0.0, 0.0);
    const Eigen::Vector3d b(1.0, 2.0, -0.5);
    const Eigen::Matrix3d rotation =
        Eigen::AngleAxisd(0.7, Eigen::Vector3d(1.0, -2.0, 0.5).normalized()).toRotationMatrix();
    const Local original = computeBar(ElementKind::Truss3D, bar, a, b);
    const Local rotated = computeBar(ElementKind::Truss3D, bar, rotation * a, rotation * b);
    Eigen::MatrixXd t = Eigen::MatrixXd::Zero(6, 6);
    t.block<3, 3>(0, 0) = rotation;
    t.block<3, 3>(3, 3) = rotation;
    const Eigen::MatrixXd expected = t * original.matrix * t.transpose();
    CHECK((rotated.matrix - expected).norm() <= 1e-12 * expected.norm());
}

// REQ-007-11: imposed strain and weight.
TEST_CASE("truss equivalent loads", "[core][truss]")
{
    const ElementKind kind = GENERATE(ElementKind::Truss2D, ElementKind::Truss3D);
    const int dims = kind == ElementKind::Truss2D ? 2 : 3;
    const double ea = 4.0e6;
    const double strain = 3.0e-4;
    const Eigen::Vector3d weight(0.5, -2.0, dims == 3 ? 1.5 : 0.0);
    const BarData bar{.axialStiffness = ea, .imposedStrain = strain, .weight = weight};
    const Eigen::Vector3d a(1.0, 1.0, 0.0);
    const Eigen::Vector3d b(4.0, 5.0, 0.0); // length 5
    const Local local = computeBar(kind, bar, a, b);

    const Eigen::Vector3d e = (b - a) / 5.0;
    const Eigen::Vector3d f1 = -ea * strain * e + 2.5 * weight;
    const Eigen::Vector3d f2 = ea * strain * e + 2.5 * weight;
    CHECK((local.vector - stacked(dims, f1, f2)).norm() <= 1e-12 * local.vector.norm());

    // The imposed elongation alone, unrestrained, gives no force.
    const Eigen::Vector3d free = strain * (b - a);
    CHECK(relative(fem::core::axialForce(bar, a, b, Eigen::Vector3d::Zero(), free), 0.0) <=
          1e-12 * ea * strain);
    // Held in place: N = -EA eps*.
    CHECK(
        relative(fem::core::axialForce(bar, a, b, Eigen::Vector3d::Zero(), Eigen::Vector3d::Zero()),
                 -ea * strain) <= 1e-12);
    // Stretched by 1e-3 of its length without imposed strain: N = EA 1e-3.
    const BarData plain{.axialStiffness = ea, .imposedStrain = 0.0, .weight = weight};
    CHECK(relative(fem::core::axialForce(plain, a, b, Eigen::Vector3d::Zero(), 1e-3 * (b - a)),
                   ea * 1e-3) <= 1e-12);
}

TEST_CASE("a truss bar of zero length is refused", "[core][truss]")
{
    const std::vector<BarData> bars(1);
    const TrussElement element(ElementKind::Truss3D, bars);
    fem::core::ElementNodes nodes;
    nodes.type = fem::core::ElementType::Line2;
    nodes.count = 2;
    nodes.x[0] = Eigen::Vector3d(1.0, 2.0, 3.0);
    nodes.x[1] = nodes.x[0];
    Eigen::MatrixXd matrix = Eigen::MatrixXd::Zero(6, 6);
    Eigen::VectorXd vector = Eigen::VectorXd::Zero(6);
    const auto result = element.compute(nodes, 0, matrix, vector);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message == "The bar has zero length.");
}
