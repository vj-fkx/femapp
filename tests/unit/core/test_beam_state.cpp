// REQ-008b-08, 13: the exact beam of design 008b §5.3.

#include <fem/core/beam_element.hpp>
#include <fem/core/beam_loads.hpp>
#include <fem/core/beam_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <array>
#include <cmath>
#include <cstddef>

using fem::core::matrixExponential;
using fem::core::StateMatrix;

namespace {

double relativeError(const StateMatrix& value, const StateMatrix& reference)
{
    return (value - reference).norm() / reference.norm();
}

} // namespace

// ADR 0006: e^A to the rounding error, for the sizes and norms of the beams.
TEST_CASE("the matrix exponential", "[core][beam][state]")
{
    SECTION("a nilpotent shift is a finite series")
    {
        const int n = 6;
        StateMatrix shift = StateMatrix::Zero(n, n);
        for (int i = 0; i + 1 < n; ++i) {
            shift(i, i + 1) = 2.0;
        }
        StateMatrix series = StateMatrix::Identity(n, n);
        StateMatrix power = StateMatrix::Identity(n, n);
        double factorial = 1.0;
        for (int k = 1; k < n; ++k) {
            power = power * shift;
            factorial *= k;
            series += power / factorial;
        }
        CHECK(relativeError(matrixExponential(shift), series) <= 1e-14);
    }

    SECTION("a diagonal matrix")
    {
        StateMatrix diagonal = StateMatrix::Zero(4, 4);
        diagonal.diagonal() << -3.0, 0.0, 0.5, 4.0;
        const StateMatrix e = matrixExponential(diagonal);
        for (int i = 0; i < 4; ++i) {
            CHECK(std::abs(e(i, i) - std::exp(diagonal(i, i))) <= 1e-14 * std::exp(diagonal(i, i)));
        }
        CHECK((e - StateMatrix(e.diagonal().asDiagonal())).norm() == 0.0);
    }

    SECTION("a skew matrix is a rotation (Rodrigues)")
    {
        const Eigen::Vector3d omega(0.3, -1.2, 2.0);
        StateMatrix skew = StateMatrix::Zero(3, 3);
        skew << 0.0, -omega.z(), omega.y(), omega.z(), 0.0, -omega.x(), -omega.y(), omega.x(), 0.0;
        const Eigen::Matrix3d rotation =
            Eigen::AngleAxisd(omega.norm(), omega.normalized()).toRotationMatrix();
        CHECK(relativeError(matrixExponential(skew), StateMatrix(rotation)) <= 1e-14);
    }

    SECTION("large norms, by scaling and squaring")
    {
        // [[a, b], [0, c]]: e^a, e^c and b (e^a - e^c) / (a - c).
        const double a = 20.0;
        const double c = -15.0;
        const double b = 3.0;
        StateMatrix m = StateMatrix::Zero(2, 2);
        m << a, b, 0.0, c;
        StateMatrix exact = StateMatrix::Zero(2, 2);
        exact << std::exp(a), b * (std::exp(a) - std::exp(c)) / (a - c), 0.0, std::exp(c);
        const StateMatrix e = matrixExponential(m);
        CHECK(std::abs(e(0, 0) - exact(0, 0)) <= 1e-13 * exact(0, 0));
        CHECK(std::abs(e(0, 1) - exact(0, 1)) <= 1e-13 * exact(0, 1));
        CHECK(std::abs(e(1, 1) - exact(1, 1)) <= 1e-13 * exact(1, 1));
        CHECK(e(1, 0) == 0.0);
    }

    SECTION("the largest size: e^A e^-A = I")
    {
        const int n = fem::core::kMaxStates;
        StateMatrix m = StateMatrix::Zero(n, n);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                m(i, j) = std::sin(1.0 + i + 3.0 * j) * 0.4;
            }
        }
        const StateMatrix product = matrixExponential(m) * matrixExponential(-m);
        CHECK((product - StateMatrix::Identity(n, n)).norm() <= 1e-12);
    }
}

namespace {

using fem::core::BeamData;
using fem::core::BeamVector;
using fem::core::SpanLoads;

BeamData sampleBeam(bool timoshenko)
{
    BeamData beam;
    beam.axial = 2.1e11 * 0.01;
    beam.torsional = 8.1e10 * 2e-6;
    beam.bendingY = 2.1e11 * 3e-5;
    beam.bendingZ = 2.1e11 * 8e-5;
    if (timoshenko) {
        beam.shearY = 8.1e10 * 0.004;
        beam.shearZ = 8.1e10 * 0.005;
    }
    beam.shearCentre = Eigen::Vector2d(0.012, -0.03);
    return beam;
}

SpanLoads sampleLoads()
{
    SpanLoads loads;
    fem::core::SpanPiece uniform;
    uniform.from = 0.0;
    uniform.to = 4.0;
    uniform.start << 1e3, -2e3, 5e2, 30.0;
    uniform.end = uniform.start;
    fem::core::SpanPiece partial;
    partial.from = 0.7;
    partial.to = 2.9;
    partial.start << 0.0, 4e3, -1e3, 0.0;
    partial.end << 2e2, -1e3, 3e3, 50.0;
    loads.pieces = {uniform, partial};
    fem::core::SpanPoint point;
    point.at = 1.3;
    point.force = Eigen::Vector3d(-5e2, 7e3, 2e3);
    point.moment = Eigen::Vector3d(40.0, -3e2, 6e2);
    loads.points = {point};
    loads.imposedStrain = 2e-4;
    loads.curvatureY = 1e-3;
    loads.curvatureZ = -2e-3;
    return loads;
}

} // namespace

// REQ-008b-08, 23: with no curvature, warping nor foundation, the exact bar
// is the closed form of the 008, stiffness and equivalent loads.
TEST_CASE("the exact bar reproduces the straight beam of the 008", "[core][beam][state]")
{
    const double length = 4.0;
    for (const bool timoshenko : {false, true}) {
        INFO("Timoshenko " << timoshenko);
        const BeamData beam = sampleBeam(timoshenko);
        const SpanLoads loads = sampleLoads();
        const auto exact = fem::core::stateBarMatrices(beam, length, loads, false);
        REQUIRE(exact.stiffness.rows() == 12);
        const fem::core::BeamMatrix reference = fem::core::flexibleStiffness(beam, length);
        CHECK((exact.stiffness - reference).norm() <= 1e-12 * reference.norm());
        const BeamVector equivalent = fem::core::equivalentLoads(beam, length, loads);
        CHECK((exact.loads - equivalent).norm() <= 1e-12 * equivalent.norm());

        // In the plane: the dofs u, v, rz of the same matrix.
        BeamData plane = beam;
        plane.shearCentre.setZero();
        const auto flat = fem::core::stateBarMatrices(plane, length, SpanLoads{}, true);
        const fem::core::BeamMatrix full = fem::core::flexibleStiffness(plane, length);
        const std::array<int, 6> dofs = {0, 1, 5, 6, 7, 11};
        for (std::size_t i = 0; i < 6; ++i) {
            for (std::size_t j = 0; j < 6; ++j) {
                CHECK(std::abs(flat.stiffness(static_cast<Eigen::Index>(i),
                                              static_cast<Eigen::Index>(j)) -
                               full(dofs.at(i), dofs.at(j))) <= 1e-12 * full.norm());
            }
        }
    }
}

// REQ-008b-08: the rigid modes have no energy, and the matrix is symmetric.
TEST_CASE("the exact bar has six rigid modes", "[core][beam][state]")
{
    BeamData beam = sampleBeam(true);
    beam.warpingStiffness = 2.1e11 * 1e-9;
    const double length = 3.0;
    const auto exact = fem::core::stateBarMatrices(beam, length, SpanLoads{}, false);
    REQUIRE(exact.stiffness.rows() == 14);
    const auto& k = exact.stiffness;
    CHECK((k - k.transpose()).norm() <= 1e-14 * k.norm());
    for (int mode = 0; mode < 6; ++mode) {
        fem::core::StateBarVector d = fem::core::StateBarVector::Zero(14);
        const Eigen::Vector3d x1 = Eigen::Vector3d::Zero();
        const Eigen::Vector3d x2(length, 0.0, 0.0);
        if (mode < 3) {
            d(mode) = 1.0;
            d(7 + mode) = 1.0;
        } else {
            Eigen::Vector3d r = Eigen::Vector3d::Zero();
            r(mode - 3) = 1.0;
            d.segment<3>(3) = r;
            d.segment<3>(10) = r;
            d.segment<3>(7) = r.cross(x2 - x1);
        }
        CHECK((k * d).norm() <= 1e-9 * k.norm());
    }
}

namespace {

// Torsion with warping, closed form (design 008b §5.3): theta = c1 + c2 x +
// c3 e^-kx + c4 e^-k(L - x), k^2 = GJ / E Iw, the stable form of sinh and
// cosh. Dofs (theta1, psi1, theta2, psi2); forces (-T, -Q) at the start and
// (T, Q) at the end, T = GJ theta' - E Iw theta''', Q = E Iw theta''.
struct WarpingClosedForm
{
    double gj;
    double eiw;
    double length;

    [[nodiscard]] double k() const { return std::sqrt(gj / eiw); }

    // Rows: theta, theta', theta'', theta''' at x, over (c1 .. c4).
    [[nodiscard]] Eigen::Matrix4d basis(double x) const
    {
        const double a = std::exp(-k() * x);
        const double b = std::exp(-k() * (length - x));
        const double kk = k();
        Eigen::Matrix4d m;
        m << 1.0, x, a, b, 0.0, 1.0, -kk * a, kk * b, 0.0, 0.0, kk * kk * a, kk * kk * b, 0.0, 0.0,
            -kk * kk * kk * a, kk * kk * kk * b;
        return m;
    }

    // Nodal forces from the derivatives (theta, theta', theta'', theta''') at
    // both ends.
    [[nodiscard]] Eigen::Vector4d forces(const Eigen::Vector4d& start,
                                         const Eigen::Vector4d& end) const
    {
        const auto torque = [this](const Eigen::Vector4d& d) { return gj * d(1) - eiw * d(3); };
        return {-torque(start), -eiw * start(2), torque(end), eiw * end(2)};
    }

    [[nodiscard]] Eigen::Matrix4d stiffness() const
    {
        const Eigen::Matrix4d b0 = basis(0.0);
        const Eigen::Matrix4d bl = basis(length);
        Eigen::Matrix4d h;
        h << b0.row(0), b0.row(1), bl.row(0), bl.row(1);
        Eigen::Matrix4d f;
        for (int c = 0; c < 4; ++c) {
            f.col(c) = forces(b0.col(c), bl.col(c));
        }
        return f * h.inverse();
    }

    // Equivalent loads of a uniform torque m, both ends held: theta_p =
    // -m x^2 / 2GJ plus the homogeneous part that holds the ends.
    [[nodiscard]] Eigen::Vector4d uniformTorque(double m) const
    {
        const auto particular = [this, m](double x) {
            return Eigen::Vector4d(-m * x * x / (2 * gj), -m * x / gj, -m / gj, 0.0);
        };
        const Eigen::Matrix4d b0 = basis(0.0);
        const Eigen::Matrix4d bl = basis(length);
        Eigen::Matrix4d h;
        h << b0.row(0), b0.row(1), bl.row(0), bl.row(1);
        const Eigen::Vector4d p0 = particular(0.0);
        const Eigen::Vector4d pl = particular(length);
        const Eigen::Vector4d c = h.inverse() * -Eigen::Vector4d(p0(0), p0(1), pl(0), pl(1));
        return -forces(b0 * c + p0, bl * c + pl);
    }
};

// The torsion dofs (rx, warping) of both ends in the 14 dofs of the exact bar.
constexpr std::array<int, 4> kTorsionDofs = {3, 6, 10, 13};

BeamData warpingBeam(double gj, double eiw)
{
    BeamData beam = sampleBeam(false);
    beam.shearCentre.setZero();
    beam.torsional = gj;
    beam.warpingStiffness = eiw;
    return beam;
}

} // namespace

// REQ-008b-07, 08: torsion with warping against the closed form, from
// uniform-torsion-like (kL = 50, subdivided) to warping-like (kL = 0.1).
TEST_CASE("torsion with warping in the exact bar", "[core][beam][state][warping]")
{
    const double length = 2.5;
    const double eiw = 2.1e11 * 1.26e-7;
    for (const double kl : {0.1, 1.0, 10.0, 50.0}) {
        INFO("kL = " << kl);
        const double gj = eiw * kl * kl / (length * length);
        const WarpingClosedForm closed{.gj = gj, .eiw = eiw, .length = length};
        SpanLoads loads;
        fem::core::SpanPiece torque;
        torque.to = length;
        torque.start << 0.0, 0.0, 0.0, 700.0;
        torque.end = torque.start;
        loads.pieces = {torque};
        const auto exact = fem::core::stateBarMatrices(warpingBeam(gj, eiw), length, loads, false);
        const Eigen::Matrix4d k = closed.stiffness();
        const Eigen::Vector4d f = closed.uniformTorque(700.0);
        for (std::size_t i = 0; i < 4; ++i) {
            const auto a = static_cast<Eigen::Index>(kTorsionDofs.at(i));
            CHECK(std::abs(exact.loads(a) - f(static_cast<Eigen::Index>(i))) <= 1e-10 * f.norm());
            for (std::size_t j = 0; j < 4; ++j) {
                const auto b = static_cast<Eigen::Index>(kTorsionDofs.at(j));
                CHECK(std::abs(exact.stiffness(a, b) -
                               k(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j))) <=
                      1e-10 * k.norm());
            }
        }
    }

    SECTION("no Saint-Venant stiffness: a bending beam of stiffness E Iw")
    {
        const double gj = 1e-12 * eiw / (length * length);
        const auto exact = fem::core::stateBarMatrices(warpingBeam(gj, eiw), length, {}, false);
        BeamData bending = sampleBeam(false);
        bending.bendingZ = eiw;
        const fem::core::BeamMatrix b = fem::core::flexibleStiffness(bending, length);
        const std::array<int, 4> plane = {1, 5, 7, 11}; // v1, rz1, v2, rz2
        for (std::size_t i = 0; i < 4; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                CHECK(std::abs(exact.stiffness(kTorsionDofs.at(i), kTorsionDofs.at(j)) -
                               b(plane.at(i), plane.at(j))) <= 1e-9 * b.norm());
            }
        }
    }

    SECTION("little warping stiffness: uniform torsion GJ / L")
    {
        const double gj = 8.1e10 * 2e-6;
        const double mu = 400.0;
        const double small = gj * length * length / (mu * mu);
        const auto exact = fem::core::stateBarMatrices(warpingBeam(gj, small), length, {}, false);
        // GJ/L mu sinh mu / (2 - 2 cosh mu + mu sinh mu) -> GJ / L mu / (mu - 2).
        CHECK(std::abs(exact.stiffness(3, 3) - gj / length * mu / (mu - 2.0)) <=
              1e-9 * gj / length);
        CHECK(std::abs(exact.stiffness(3, 10) + gj / length * mu / (mu - 2.0)) <=
              1e-9 * gj / length);
    }
}

// REQ-008b-11: an elastic foundation of torsion on a beam with warping and
// no Saint-Venant stiffness is Hetenyi's beam: theta = T beta / (2 k) under
// a concentrated torque, beta = (k / 4 E Iw)^(1/4).
TEST_CASE("warping beam on a torsional foundation", "[core][beam][state][warping]")
{
    const double eiw = 2.1e11 * 1.26e-7;
    const double kt = 5e5;
    const double beta = std::pow(kt / (4 * eiw), 0.25);
    const double half = 20.0 / beta; // beta L = 40: the ends are far
    BeamData beam = warpingBeam(0.0, eiw);
    beam.foundation(3) = kt;
    const auto bar = fem::core::stateBarMatrices(beam, half, {}, false);
    // Two bars, three nodes, the torsion dofs only (uncoupled).
    Eigen::Matrix<double, 6, 6> k = Eigen::Matrix<double, 6, 6>::Zero();
    for (int e = 0; e < 2; ++e) {
        for (std::size_t i = 0; i < 4; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                k(2 * e + static_cast<int>(i), 2 * e + static_cast<int>(j)) +=
                    bar.stiffness(kTorsionDofs.at(i), kTorsionDofs.at(j));
            }
        }
    }
    Eigen::Matrix<double, 6, 1> f = Eigen::Matrix<double, 6, 1>::Zero();
    const double torque = 1e3;
    f(2) = torque;
    const Eigen::Matrix<double, 6, 1> d = k.ldlt().solve(f);
    CHECK(std::abs(d(2) - torque * beta / (2 * kt)) <= 1e-8 * torque * beta / (2 * kt));
    CHECK(std::abs(d(3)) <= 1e-8 * beta * d(2));
}

namespace {

// The frame of an arc with turn rate omega at s, from the start frame, and
// the position of its axis (augmented exponential of [[omega x, e1], [0, 0]]).
struct ArcGeometry
{
    Eigen::Matrix3d frame;
    Eigen::Vector3d position;
};

ArcGeometry arcAt(const Eigen::Vector3d& omega, double s)
{
    fem::core::StateMatrix m = fem::core::StateMatrix::Zero(4, 4);
    m(0, 1) = -omega.z();
    m(0, 2) = omega.y();
    m(1, 0) = omega.z();
    m(1, 2) = -omega.x();
    m(2, 0) = -omega.y();
    m(2, 1) = omega.x();
    m(0, 3) = 1.0;
    const fem::core::StateMatrix e = fem::core::matrixExponential(s * m);
    return {.frame = e.topLeftCorner(3, 3), .position = e.topRightCorner(3, 1)};
}

} // namespace

// REQ-008b-13, 15: an arc with its curvature in any direction of the
// section has the six rigid modes, and tends to the straight bar.
TEST_CASE("the exact arc", "[core][beam][state][arc]")
{
    const double length = 3.0;
    const Eigen::Vector3d omega(0.0, 0.2, 0.35);
    BeamData beam = sampleBeam(true);
    beam.warpingStiffness = 2.1e11 * 1e-9;
    beam.curvature = omega;
    const auto arc = fem::core::stateBarMatrices(beam, length, SpanLoads{}, false);
    const auto& k = arc.stiffness;
    CHECK((k - k.transpose()).norm() <= 1e-14 * k.norm());
    const ArcGeometry end = arcAt(omega, length);
    const Eigen::Matrix3d r = end.frame;
    for (int mode = 0; mode < 6; ++mode) {
        Eigen::Vector3d t = Eigen::Vector3d::Zero();
        Eigen::Vector3d phi = Eigen::Vector3d::Zero();
        (mode < 3 ? t : phi)(mode % 3) = 1.0;
        fem::core::StateBarVector d = fem::core::StateBarVector::Zero(14);
        d.segment<3>(0) = t;
        d.segment<3>(3) = phi;
        d.segment<3>(7) = r.transpose() * (t + phi.cross(end.position));
        d.segment<3>(10) = r.transpose() * phi;
        INFO("mode " << mode);
        CHECK((k * d).norm() <= 1e-9 * k.norm());
    }

    SECTION("a large radius gives the straight bar")
    {
        BeamData flat = beam;
        flat.curvature = Eigen::Vector3d(0.0, 0.0, 1e-9);
        BeamData straight = beam;
        straight.curvature.setZero();
        const auto a = fem::core::stateBarMatrices(flat, length, SpanLoads{}, false);
        const auto b = fem::core::stateBarMatrices(straight, length, SpanLoads{}, false);
        CHECK((a.stiffness - b.stiffness).norm() <= 1e-6 * b.stiffness.norm());
    }
}

// REQ-008b-15: a force fixed in space on an arc, through the augmented
// states: its equivalent loads balance it exactly, and agree with many
// local pieces that follow its turning components.
TEST_CASE("forces fixed in space on an arc", "[core][beam][state][arc]")
{
    const double length = 3.0;
    const Eigen::Vector3d omega(0.0, 0.2, 0.35);
    BeamData beam = sampleBeam(false);
    beam.curvature = omega;
    SpanLoads loads;
    loads.fixedPieces = {{.from = 0.0,
                          .to = length,
                          .start = Eigen::Vector3d(1e3, -2e3, 5e2),
                          .end = Eigen::Vector3d(1e3, -2e3, 5e2)},
                         {.from = 0.5,
                          .to = 2.0,
                          .start = Eigen::Vector3d(0.0, 3e3, 0.0),
                          .end = Eigen::Vector3d(-1e3, 0.0, 2e3)}};
    const auto exact = fem::core::stateBarMatrices(beam, length, loads, false);
    const auto& f = exact.loads;

    // The load in the start frame (constant there) and its resultants, by
    // 20-point Gauss on each interval of its pieces.
    const auto load = [&loads](double s) {
        Eigen::Vector3d q = Eigen::Vector3d::Zero();
        for (const auto& piece : loads.fixedPieces) {
            if (s >= piece.from && s <= piece.to) {
                q += piece.start +
                     (piece.end - piece.start) * (s - piece.from) / (piece.to - piece.from);
            }
        }
        return q;
    };
    Eigen::Vector3d force = Eigen::Vector3d::Zero();
    Eigen::Vector3d moment = Eigen::Vector3d::Zero();
    const std::array<double, 4> breaks = {0.0, 0.5, 2.0, length};
    for (std::size_t i = 0; i + 1 < breaks.size(); ++i) {
        const int points = 400;
        for (int g = 0; g < points; ++g) {
            // Midpoint rule on a fine grid of a smooth integrand.
            const double h = (breaks.at(i + 1) - breaks.at(i)) / points;
            const double s = breaks.at(i) + (g + 0.5) * h;
            const Eigen::Vector3d q = load(s);
            force += h * q;
            moment += h * arcAt(omega, s).position.cross(q);
        }
    }
    const ArcGeometry end = arcAt(omega, length);
    const Eigen::Vector3d endForce = end.frame * f.segment<3>(6);
    const Eigen::Vector3d sumForce = f.segment<3>(0) + endForce;
    const Eigen::Vector3d sumMoment =
        f.segment<3>(3) + end.frame * f.segment<3>(9) + end.position.cross(endForce);
    CHECK((sumForce - force).norm() <= 1e-5 * force.norm());
    CHECK((sumMoment - moment).norm() <= 1e-5 * moment.norm());

    // Many local pieces, linear between the turned components.
    SpanLoads pieces;
    const int count = 600;
    for (int i = 0; i < count; ++i) {
        const double a = length * i / count;
        const double b = length * (i + 1) / count;
        fem::core::SpanPiece piece;
        piece.from = a;
        piece.to = b;
        // Just inside the interval: the partial piece jumps at its ends.
        piece.start << arcAt(omega, a).frame.transpose() * load(a + 1e-12), 0.0;
        piece.end << arcAt(omega, b).frame.transpose() * load(b - 1e-12), 0.0;
        // The lever about the shear centre that the 008 pieces carry.
        piece.start(3) =
            beam.shearCentre.y() * piece.start(1) - beam.shearCentre.x() * piece.start(2);
        piece.end(3) = beam.shearCentre.y() * piece.end(1) - beam.shearCentre.x() * piece.end(2);
        pieces.pieces.push_back(piece);
    }
    const auto approximate = fem::core::stateBarMatrices(beam, length, pieces, false);
    CHECK((approximate.loads - f).norm() <= 1e-4 * f.norm());
}

namespace {

// The loads of [0, x] only (pieces cut at x), for a shorter bar.
SpanLoads cutAt(const SpanLoads& loads, double x)
{
    SpanLoads cut;
    cut.imposedStrain = loads.imposedStrain;
    cut.curvatureY = loads.curvatureY;
    cut.curvatureZ = loads.curvatureZ;
    for (const auto& piece : loads.pieces) {
        if (piece.from < x) {
            fem::core::SpanPiece p = piece;
            if (piece.to > x) {
                p.to = x;
                p.end = piece.start +
                        (piece.end - piece.start) * (x - piece.from) / (piece.to - piece.from);
            }
            cut.pieces.push_back(p);
        }
    }
    for (const auto& piece : loads.fixedPieces) {
        if (piece.from < x) {
            fem::core::SpanFixedPiece p = piece;
            if (piece.to > x) {
                p.to = x;
                p.end = piece.start +
                        (piece.end - piece.start) * (x - piece.from) / (piece.to - piece.from);
            }
            cut.fixedPieces.push_back(p);
        }
    }
    for (const auto& point : loads.points) {
        if (point.at <= x) {
            cut.points.push_back(point);
        }
    }
    return cut;
}

} // namespace

// REQ-008b-19: the state anywhere along the exact bar, against the ends and
// against the same bar cut shorter, across the internal subdivision.
TEST_CASE("the state along the exact bar", "[core][beam][state]")
{
    const double length = 3.0;
    BeamData beam = sampleBeam(true);
    beam.curvature = Eigen::Vector3d(0.0, 0.2, 0.35);
    beam.torsional = 8.1e10 * 2e-6;
    beam.warpingStiffness = beam.torsional * length * length / (12.0 * 12.0); // kL = 12: parts
    SpanLoads loads = sampleLoads();
    loads.pieces[0].to = length;
    loads.points.push_back(
        {.at = 0.0, .force = Eigen::Vector3d(1e2, 2e2, 3e2), .moment = Eigen::Vector3d::Zero()});
    loads.points.push_back({.at = length,
                            .force = Eigen::Vector3d(0.0, -4e2, 0.0),
                            .moment = Eigen::Vector3d(10.0, 0.0, 0.0)});
    loads.points.push_back(
        {.at = 1.5, .force = Eigen::Vector3d(0.0, 0.0, 9e2), .moment = Eigen::Vector3d::Zero()});
    loads.fixedPieces = {{.from = 0.4,
                          .to = 2.2,
                          .start = Eigen::Vector3d(3e2, 0.0, -6e2),
                          .end = Eigen::Vector3d(0.0, 5e2, 0.0)}};
    const auto bar = fem::core::stateBarMatrices(beam, length, loads, false);
    fem::core::StateBarVector ends(14);
    for (int i = 0; i < 14; ++i) {
        ends(i) = 1e-3 * std::sin(1.0 + 2.0 * i);
    }
    const fem::core::StateBarVector nodal = bar.stiffness * ends - bar.loads;
    const auto unpack = [](const fem::core::BarState& s) {
        fem::core::StateBarVector v(7);
        v << s.force, s.moment, -s.bimoment;
        return v;
    };
    const double scale = nodal.norm();

    const fem::core::BarState first =
        fem::core::stateAt(beam, length, loads, false, ends, 0.0, fem::core::Side::Before);
    CHECK((first.displacement - ends.segment<3>(0)).norm() <= 1e-12);
    CHECK((unpack(first) + nodal.head(7)).norm() <= 1e-9 * scale);
    const fem::core::BarState last =
        fem::core::stateAt(beam, length, loads, false, ends, length, fem::core::Side::After);
    CHECK((last.rotation - ends.segment<3>(10)).norm() <= 1e-12);
    CHECK(std::abs(last.warping - ends(13)) <= 1e-12);
    CHECK((unpack(last) - nodal.tail(7)).norm() <= 1e-9 * scale);

    for (const double x : {0.37, 1.5, 2.71}) {
        INFO("x = " << x);
        const fem::core::BarState here =
            fem::core::stateAt(beam, length, loads, false, ends, x, fem::core::Side::After);
        const SpanLoads cut = cutAt(loads, x);
        const auto shorter = fem::core::stateBarMatrices(beam, x, cut, false);
        fem::core::StateBarVector d(14);
        d.head(7) = ends.head(7);
        d.segment<3>(7) = here.displacement;
        d.segment<3>(10) = here.rotation;
        d(13) = here.warping;
        const fem::core::StateBarVector p = shorter.stiffness * d - shorter.loads;
        CHECK((unpack(here) - p.tail(7)).norm() <= 1e-9 * scale);
        CHECK((p.head(7) - nodal.head(7)).norm() <= 1e-9 * scale);
    }

    // The jump at the concentrated load at 1.5.
    const auto before =
        fem::core::stateAt(beam, length, loads, false, ends, 1.5, fem::core::Side::Before);
    const auto after =
        fem::core::stateAt(beam, length, loads, false, ends, 1.5, fem::core::Side::After);
    CHECK((before.force - after.force - Eigen::Vector3d(0.0, 0.0, 9e2)).norm() <= 1e-9 * scale);
    CHECK((before.displacement - after.displacement).norm() <= 1e-15);
}
