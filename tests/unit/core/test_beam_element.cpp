// REQ-008-05 to 15: the beam element on its own: local axes, stiffness of
// both theories, rigid body modes, shear centre, offsets, connections and
// elastic foundation.

#include <fem/core/beam_element.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using fem::core::BeamData;
using fem::core::BeamElement;
using fem::core::BeamMatrix;
using fem::core::BeamVector;
using fem::core::ElementKind;

namespace {

constexpr double kPi = std::numbers::pi;

// A beam with distinct stiffnesses, so that swapped terms show.
BeamData sampleBeam()
{
    BeamData beam;
    beam.axial = 4.0e6;
    beam.torsional = 3.0e3;
    beam.bendingY = 2.0e4;
    beam.bendingZ = 5.0e4;
    return beam;
}

Eigen::MatrixXd compute(ElementKind kind, const BeamData& beam, const Eigen::Vector3d& a,
                        const Eigen::Vector3d& b, Eigen::VectorXd* vector = nullptr)
{
    const std::vector<BeamData> beams = {beam};
    const BeamElement element(kind, beams);
    const int size = kind == ElementKind::Beam2D ? 6 : 12;
    Eigen::MatrixXd matrix = Eigen::MatrixXd::Zero(size, size);
    Eigen::VectorXd local = Eigen::VectorXd::Zero(size);
    fem::core::ElementNodes nodes;
    nodes.type = fem::core::ElementType::Line2;
    nodes.count = 2;
    nodes.x[0] = a;
    nodes.x[1] = b;
    REQUIRE(element.compute(nodes, 0, matrix, local).has_value());
    if (vector != nullptr) {
        *vector = local;
    }
    return matrix;
}

// The textbook 3D frame matrix in local axes (McGuire, Gallagher and Ziemian,
// Matrix Structural Analysis, eq. 4.34, with the shear terms of
// Przemieniecki), dofs u v w rx ry rz per end.
BeamMatrix textbook(const BeamData& beam, double l)
{
    const double py = fem::core::shearParameter(beam, l, true);
    const double pz = fem::core::shearParameter(beam, l, false);
    const double ez = beam.bendingZ / (1.0 + py);
    const double ey = beam.bendingY / (1.0 + pz);
    BeamMatrix k = BeamMatrix::Zero();
    auto set = [&k](int i, int j, double v) {
        k(i, j) = v;
        k(j, i) = v;
    };
    set(0, 0, beam.axial / l);
    set(0, 6, -beam.axial / l);
    set(6, 6, beam.axial / l);
    set(3, 3, beam.torsional / l);
    set(3, 9, -beam.torsional / l);
    set(9, 9, beam.torsional / l);
    // x-y plane: v, rz.
    set(1, 1, 12 * ez / (l * l * l));
    set(1, 5, 6 * ez / (l * l));
    set(1, 7, -12 * ez / (l * l * l));
    set(1, 11, 6 * ez / (l * l));
    set(5, 5, (4 + py) * ez / l);
    set(5, 7, -6 * ez / (l * l));
    set(5, 11, (2 - py) * ez / l);
    set(7, 7, 12 * ez / (l * l * l));
    set(7, 11, -6 * ez / (l * l));
    set(11, 11, (4 + py) * ez / l);
    // x-z plane: w, ry.
    set(2, 2, 12 * ey / (l * l * l));
    set(2, 4, -6 * ey / (l * l));
    set(2, 8, -12 * ey / (l * l * l));
    set(2, 10, -6 * ey / (l * l));
    set(4, 4, (4 + pz) * ey / l);
    set(4, 8, 6 * ey / (l * l));
    set(4, 10, (2 - pz) * ey / l);
    set(8, 8, 12 * ey / (l * l * l));
    set(8, 10, 6 * ey / (l * l));
    set(10, 10, (4 + pz) * ey / l);
    return k;
}

BeamMatrix rotation(const Eigen::Matrix3d& axes)
{
    BeamMatrix t = BeamMatrix::Zero();
    for (Eigen::Index i = 0; i < 4; ++i) {
        t.block<3, 3>(3 * i, 3 * i) = axes.transpose();
    }
    return t;
}

// u_i = t + w x x_i, rotations w, at both nodes.
BeamVector rigidMotion(const Eigen::Vector3d& t, const Eigen::Vector3d& w, const Eigen::Vector3d& a,
                       const Eigen::Vector3d& b)
{
    BeamVector u;
    u << t + w.cross(a), w, t + w.cross(b), w;
    return u;
}

double relativeNorm(const Eigen::MatrixXd& value, const Eigen::MatrixXd& reference)
{
    return (value - reference).norm() / reference.norm();
}

} // namespace

// REQ-008-09, 10
TEST_CASE("beam local axes", "[core][beam]")
{
    using fem::core::beamAxes;
    const Eigen::Vector3d o = Eigen::Vector3d::Zero();
    const auto axesOf = [](const auto& result) {
        REQUIRE(result.has_value());
        return result.value_or(Eigen::Matrix3d::Zero());
    };

    // Horizontal bar, default reference Z: y up, as in 2D.
    const Eigen::Matrix3d h =
        axesOf(beamAxes(o, Eigen::Vector3d(2, 0, 0), std::nullopt, 0.0, ElementKind::Beam3D));
    CHECK((h.col(0) - Eigen::Vector3d::UnitX()).norm() <= 1e-15);
    CHECK((h.col(1) - Eigen::Vector3d::UnitZ()).norm() <= 1e-15);
    CHECK((h.col(2) + Eigen::Vector3d::UnitY()).norm() <= 1e-15);

    // Vertical bar: the default turns to global X.
    const Eigen::Matrix3d v =
        axesOf(beamAxes(o, Eigen::Vector3d(0, 0, 3), std::nullopt, 0.0, ElementKind::Beam3D));
    CHECK((v.col(1) - Eigen::Vector3d::UnitX()).norm() <= 1e-15);
    CHECK((v.col(2) - Eigen::Vector3d::UnitY()).norm() <= 1e-15);

    // A given reference, not normal to the bar, and a roll.
    const Eigen::Vector3d x2(1, 1, 0);
    const Eigen::Matrix3d r =
        axesOf(beamAxes(o, x2, Eigen::Vector3d(1, 0, 1), 0.0, ElementKind::Beam3D));
    CHECK(std::abs(r.col(1).dot(x2)) <= 1e-15);
    CHECK(r.col(1).dot(Eigen::Vector3d(1, 0, 1)) > 0.0);
    CHECK((r.transpose() * r - Eigen::Matrix3d::Identity()).norm() <= 1e-14);
    CHECK(r.determinant() > 0.0);
    const Eigen::Matrix3d rolled =
        axesOf(beamAxes(o, Eigen::Vector3d(2, 0, 0), std::nullopt, kPi / 2, ElementKind::Beam3D));
    CHECK((rolled.col(1) + Eigen::Vector3d::UnitY()).norm() <= 1e-15);
    CHECK((rolled.col(2) + Eigen::Vector3d::UnitZ()).norm() <= 1e-15);

    // 2D: y = Z x x.
    const Eigen::Matrix3d p =
        axesOf(beamAxes(o, Eigen::Vector3d(0, -1, 0), std::nullopt, 0.0, ElementKind::Beam2D));
    CHECK((p.col(1) - Eigen::Vector3d::UnitX()).norm() <= 1e-15);
    CHECK((p.col(2) - Eigen::Vector3d::UnitZ()).norm() <= 1e-15);

    const auto parallel =
        beamAxes(o, Eigen::Vector3d(1, 0, 0), Eigen::Vector3d(-3, 0, 0), 0.0, ElementKind::Beam3D);
    REQUIRE_FALSE(parallel.has_value());
    CHECK(parallel.error().message == "The reference vector is parallel to the bar.");
    CHECK_FALSE(beamAxes(o, o, std::nullopt, 0.0, ElementKind::Beam3D).has_value());
}

TEST_CASE("beam dofs per node", "[core][beam]")
{
    using fem::core::DofType;
    using fem::core::mask;
    const std::vector<BeamData> beams(1);
    CHECK(BeamElement(ElementKind::Beam2D, beams).dofsAt(fem::core::ElementType::Line2, 0) ==
          (mask(DofType::U) | mask(DofType::V) | mask(DofType::RZ)));
    CHECK(BeamElement(ElementKind::Beam3D, beams).dofsAt(fem::core::ElementType::Line2, 1) ==
          0b111111);
}

// REQ-008-05 to 07: the closed form in any orientation, both theories.
TEST_CASE("beam stiffness", "[core][beam]")
{
    const bool timoshenko = GENERATE(false, true);
    BeamData beam = sampleBeam();
    if (timoshenko) {
        beam.shearY = 1.5e5;
        beam.shearZ = 0.9e5;
    }
    const Eigen::Vector3d a(0.3, -1.2, 0.5);
    const Eigen::Vector3d b(2.1, 0.4, 1.7);
    const auto axes =
        fem::core::beamAxes(a, b, Eigen::Vector3d(0.2, 1.0, -0.4), 0.3, ElementKind::Beam3D);
    REQUIRE(axes.has_value());
    beam.axes = axes.value_or(Eigen::Matrix3d::Identity());
    const double l = (b - a).norm();

    const Eigen::MatrixXd k = compute(ElementKind::Beam3D, beam, a, b);
    const BeamMatrix t = rotation(beam.axes);
    const BeamMatrix expected = t.transpose() * textbook(beam, l) * t;
    CHECK(relativeNorm(k, expected) <= 1e-12);
    CHECK(relativeNorm(k, k.transpose()) <= 1e-14);

    // Six rigid body modes with zero energy, the rest positive.
    const double scale = expected.norm();
    for (int i = 0; i < 3; ++i) {
        const Eigen::Vector3d e = Eigen::Vector3d::Unit(i);
        CHECK((k * rigidMotion(e, Eigen::Vector3d::Zero(), a, b)).norm() <= 1e-12 * scale);
        CHECK((k * rigidMotion(Eigen::Vector3d::Zero(), e, a, b)).norm() <= 1e-12 * scale);
    }
    const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eigen(k);
    CHECK(eigen.eigenvalues()(5) <= 1e-10 * scale);
    CHECK(eigen.eigenvalues()(6) > 1e-6 * scale);
}

// REQ-008-05: rotating the bar rotates its matrix.
TEST_CASE("beam stiffness is invariant under rotation", "[core][beam]")
{
    BeamData beam = sampleBeam();
    beam.shearY = 2e5;
    const Eigen::Vector3d a(0.0, 0.0, 0.0);
    const Eigen::Vector3d b(1.0, 2.0, -0.5);
    const Eigen::Vector3d ref(0.0, 0.0, 1.0);
    const Eigen::Matrix3d r =
        Eigen::AngleAxisd(0.7, Eigen::Vector3d(1.0, -2.0, 0.5).normalized()).toRotationMatrix();
    beam.axes = fem::core::beamAxes(a, b, ref, 0.0, ElementKind::Beam3D)
                    .value_or(Eigen::Matrix3d::Identity());
    const Eigen::MatrixXd original = compute(ElementKind::Beam3D, beam, a, b);
    beam.axes = fem::core::beamAxes(r * a, r * b, r * ref, 0.0, ElementKind::Beam3D)
                    .value_or(Eigen::Matrix3d::Identity());
    const Eigen::MatrixXd rotated = compute(ElementKind::Beam3D, beam, r * a, r * b);
    BeamMatrix t = BeamMatrix::Zero();
    for (Eigen::Index i = 0; i < 4; ++i) {
        t.block<3, 3>(3 * i, 3 * i) = r;
    }
    CHECK(relativeNorm(rotated, t * original * t.transpose()) <= 1e-12);
}

// REQ-008-05, 06: the 2D beam is the in-plane part of the 3D one.
TEST_CASE("2D beam stiffness and rigid modes", "[core][beam]")
{
    BeamData beam = sampleBeam();
    beam.shearY = 1e5;
    const Eigen::Vector3d a(0.5, 0.2, 0.0);
    const Eigen::Vector3d b(-1.0, 1.7, 0.0);
    beam.axes = fem::core::beamAxes(a, b, std::nullopt, 0.0, ElementKind::Beam2D)
                    .value_or(Eigen::Matrix3d::Identity());
    const Eigen::MatrixXd k2 = compute(ElementKind::Beam2D, beam, a, b);
    const Eigen::MatrixXd k3 = compute(ElementKind::Beam3D, beam, a, b);
    const std::array<int, 6> planar = {0, 1, 5, 6, 7, 11};
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            CHECK(k2(i, j) ==
                  k3(planar[static_cast<std::size_t>(i)], planar[static_cast<std::size_t>(j)]));
        }
    }
    const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eigen(k2);
    CHECK(std::abs(eigen.eigenvalues()(2)) <= 1e-10 * k2.norm());
    CHECK(eigen.eigenvalues()(3) > 1e-6 * k2.norm());
}

// REQ-008-07: Timoshenko tends to Euler-Bernoulli without locking.
TEST_CASE("a Timoshenko beam with stiff shear is an Euler-Bernoulli beam", "[core][beam]")
{
    const BeamData euler = sampleBeam();
    BeamData stiff = euler;
    stiff.shearY = stiff.shearZ = 1e12 * euler.axial;
    const Eigen::Vector3d a(0.0, 0.0, 0.0);
    const Eigen::Vector3d b(3.0, 0.0, 0.0);
    CHECK(relativeNorm(compute(ElementKind::Beam3D, stiff, a, b),
                       compute(ElementKind::Beam3D, euler, a, b)) <= 1e-10);
    // A short Timoshenko beam is clearly softer in bending.
    BeamData shear = euler;
    shear.shearY = 1e3;
    const Eigen::MatrixXd ks = compute(ElementKind::Beam3D, shear, a, b);
    CHECK(ks(7, 7) < 0.9 * compute(ElementKind::Beam3D, euler, a, b)(7, 7));
}

// REQ-008-05, 14: the shear centre and the offsets keep the rigid body modes.
TEST_CASE("beam with shear centre and rigid offsets", "[core][beam]")
{
    BeamData beam = sampleBeam();
    beam.shearY = 3e5;
    beam.shearZ = 2e5;
    beam.shearCentre = Eigen::Vector2d(0.05, -0.12);
    const Eigen::Vector3d a(0.0, 0.0, 0.0);
    const Eigen::Vector3d b(4.0, 1.0, 0.5);
    beam.offsets = {Eigen::Vector3d(0.2, 0.1, -0.3), Eigen::Vector3d(-0.4, 0.0, 0.2)};
    const Eigen::Vector3d p = a + beam.offsets[0];
    const Eigen::Vector3d q = b + beam.offsets[1];
    beam.axes = fem::core::beamAxes(p, q, std::nullopt, 0.4, ElementKind::Beam3D)
                    .value_or(Eigen::Matrix3d::Identity());
    const Eigen::MatrixXd k = compute(ElementKind::Beam3D, beam, a, b);
    const double scale = k.norm();
    for (int i = 0; i < 3; ++i) {
        const Eigen::Vector3d e = Eigen::Vector3d::Unit(i);
        CHECK((k * rigidMotion(e, Eigen::Vector3d::Zero(), a, b)).norm() <= 1e-11 * scale);
        CHECK((k * rigidMotion(Eigen::Vector3d::Zero(), e, a, b)).norm() <= 1e-11 * scale);
    }
    const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eigen(k);
    CHECK(eigen.eigenvalues()(6) > 1e-8 * scale);

    // Without offsets and shear centre, the plain matrix.
    BeamData plain = sampleBeam();
    plain.shearY = beam.shearY;
    plain.shearZ = beam.shearZ;
    plain.axes = fem::core::beamAxes(a, b, std::nullopt, 0.4, ElementKind::Beam3D)
                     .value_or(Eigen::Matrix3d::Identity());
    const BeamMatrix t = rotation(plain.axes);
    CHECK(relativeNorm(compute(ElementKind::Beam3D, plain, a, b),
                       t.transpose() * textbook(plain, (b - a).norm()) * t) <= 1e-12);

    // An offset along the bar is the same as a shorter bar.
    BeamData along = plain;
    along.axes = Eigen::Matrix3d::Identity();
    along.offsets = {Eigen::Vector3d(0.5, 0.0, 0.0), Eigen::Vector3d::Zero()};
    BeamData shorter = plain;
    shorter.axes = Eigen::Matrix3d::Identity();
    const Eigen::MatrixXd ka =
        compute(ElementKind::Beam3D, along, Eigen::Vector3d::Zero(), Eigen::Vector3d(3, 0, 0));
    const Eigen::MatrixXd ks =
        compute(ElementKind::Beam3D, shorter, Eigen::Vector3d(0.5, 0, 0), Eigen::Vector3d(3, 0, 0));
    // Same axial and torsional terms; bending moments carried by the link.
    CHECK(std::abs(ka(0, 0) - ks(0, 0)) <= 1e-12 * ks(0, 0));
    CHECK(std::abs(ka(3, 3) - ks(3, 3)) <= 1e-12 * ks(3, 3));
    CHECK(std::abs(ka(7, 7) - ks(7, 7)) <= 1e-12 * ks(7, 7));
}

// REQ-008-11 to 13: connections condensed in the element.
TEST_CASE("beam end connections", "[core][beam]")
{
    const BeamData base = sampleBeam(); // along X, local axes = global
    const Eigen::Vector3d a(0.0, 0.0, 0.0);
    const Eigen::Vector3d b(2.0, 0.0, 0.0);
    const double l = 2.0;
    const double ei = base.bendingZ;

    SECTION("a moment release gives the fixed-pinned matrix")
    {
        BeamData hinge = base;
        hinge.springs[1][5] = 0.0; // Mz at the end
        const Eigen::MatrixXd k = compute(ElementKind::Beam3D, hinge, a, b);
        // On (v1, rz1, v2): 3EI/L^3 [1 L -1; L L^2 -L; -1 -L 1], rz2 free.
        const std::array<int, 3> dofs = {1, 5, 7};
        Eigen::Matrix3d expected;
        expected << 1, l, -1, l, l * l, -l, -1, -l, 1;
        expected *= 3 * ei / (l * l * l);
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                CHECK(std::abs(
                          k(dofs[static_cast<std::size_t>(i)], dofs[static_cast<std::size_t>(j)]) -
                          expected(i, j)) <= 1e-10 * expected.norm());
            }
        }
        CHECK(k.row(11).norm() <= 1e-10 * expected.norm());
        CHECK(fem::core::hasStableConnections(hinge, l, ElementKind::Beam3D));
    }
    SECTION("a stiff spring is a rigid connection")
    {
        BeamData spring = base;
        spring.springs[0][5] = 1e14;
        spring.springs[1][1] = 1e16;
        CHECK(relativeNorm(compute(ElementKind::Beam3D, spring, a, b),
                           compute(ElementKind::Beam3D, base, a, b)) <= 1e-8);
    }
    SECTION("a bar pinned at both ends has no transverse stiffness")
    {
        BeamData pinned = base;
        for (auto& end : pinned.springs) {
            end[4] = 0.0;
            end[5] = 0.0;
        }
        const Eigen::MatrixXd k = compute(ElementKind::Beam3D, pinned, a, b);
        BeamVector lift = BeamVector::Zero();
        lift(7) = 1.0; // v2
        lift(8) = 1.0; // w2
        CHECK((k * lift).norm() <= 1e-10 * k.norm());
        CHECK(k(0, 0) == base.axial / l);
        CHECK(fem::core::hasStableConnections(pinned, l, ElementKind::Beam3D));
    }
    SECTION("unstable releases are detected")
    {
        BeamData torsion = base;
        torsion.springs[0][3] = 0.0;
        torsion.springs[1][3] = 0.0;
        CHECK_FALSE(fem::core::hasStableConnections(torsion, l, ElementKind::Beam3D));
        BeamData spin = base;
        spin.springs[0][5] = spin.springs[1][5] = 0.0;
        spin.springs[1][1] = 0.0; // and Vy: the bar turns about z
        CHECK_FALSE(fem::core::hasStableConnections(spin, l, ElementKind::Beam3D));
        CHECK_FALSE(fem::core::hasStableConnections(spin, l, ElementKind::Beam2D));
        // A torsion release does not count in 2D.
        CHECK(fem::core::hasStableConnections(torsion, l, ElementKind::Beam2D));
        // A foundation holds the bar.
        spin.foundation = Eigen::Vector4d(0.0, 1e3, 0.0, 0.0);
        CHECK(fem::core::hasStableConnections(spin, l, ElementKind::Beam3D));
    }
}

// REQ-008-15: the consistent foundation matrix.
TEST_CASE("beam on an elastic foundation", "[core][beam]")
{
    const bool timoshenko = GENERATE(false, true);
    BeamData beam; // no stiffness of its own
    beam.foundation = Eigen::Vector4d(2.0, 3.0, 5.0, 7.0);
    if (timoshenko) {
        beam.bendingY = beam.bendingZ = 1.0;
        beam.shearY = beam.shearZ = 4.0;
    }
    const double l = 1.5;
    const Eigen::MatrixXd k =
        compute(ElementKind::Beam3D, beam, Eigen::Vector3d::Zero(), Eigen::Vector3d(l, 0, 0));
    CHECK(relativeNorm(k, k.transpose()) <= 1e-14);
    if (!timoshenko) {
        const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eigen(k);
        CHECK(eigen.eigenvalues()(0) > 0.0);
    }
    // A uniform translation (or twist) stores k L per unit squared: the
    // interpolations add up to one and a rigid motion bends nothing.
    for (int i = 0; i < 4; ++i) {
        BeamVector u = BeamVector::Zero();
        u(i) = u(6 + i) = 1.0;
        CHECK(std::abs(u.dot(k * u) - beam.foundation(i) * l) <= 1e-12 * beam.foundation(i) * l);
    }
    // Consistent with the Hermite functions: k_v1v1 = 13 ky L / 35.
    if (!timoshenko) {
        CHECK(std::abs(k(1, 1) - 13.0 * 3.0 * l / 35.0) <= 1e-12);
    }
}
