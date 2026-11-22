// REQ-008b-08, 11, 13: the exact beam by its state equations (design 008b
// §5.3). States in the principal local axes of the moving frame, on the
// centroid axis:
//   displacements u (3), rotations theta (3), warping psi (rate of twist),
//   forces F (N, Vy, Vz), moments M (Mx about the centroid, My, Mz), and
//   Q = E Iw psi', the conjugate of psi (the bimoment is B = -Q).
// With the turn rate Omega of the frame, a vector derivative is d/ds + Omega x:
//   u' = -Omega x u - e1 x theta + Gamma,  theta' = -Omega x theta + K,
//   F' = -Omega x F - q,                   M' = -Omega x M - e1 x F - m,
//   psi' = Q / E Iw,                       Q' = G J psi - T_S,
// where T_S = Mx + ez Vy - ey Vz is the torque about the shear centre, the
// twist rate is kx = psi (warping) or T_S / G J, and
//   Gamma = (N / EA, Vy / G Asy + ez kx, Vz / G Asz - ey kx),
//   K = (kx, My / EIy, Mz / EIz),
// plus the imposed strains, and the foundation adds k u to F' and k_t rx
// to Mx'. Euler-Bernoulli drops the shear compliance.
// Feature 008c (design 008c §5.1): with the axial force N of the reference
// state, the slope of the centroid axis is, by Engesser,
//   sl = (-e1 x theta + F_perp / G As) / (1 + N / G As)
// (in Gamma, without the twist about the shear centre), and the moment
// equation gains M' -= sl x (N e1), the equilibrium on the deformed axis.

#include <fem/core/beam_state.hpp>

#include <Eigen/Cholesky>
#include <Eigen/Eigenvalues>
#include <Eigen/LU>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numbers>
#include <unsupported/Eigen/MatrixFunctions>
#include <vector>

namespace fem::core {

StateMatrix matrixExponential(const StateMatrix& a)
{
    // ADR 0006: the only use of the module, so that it can be replaced.
    return a.exp();
}

namespace {

// The largest growth e^(rate h) of the homogeneous solutions within one
// part: the transfer matrix of a part then loses about rate h / ln 10
// digits when the stiffness is drawn from it (design 008b §5.3).
constexpr double kGrowthPerPart = 8.0;

// Indices of the full state.
enum Full : std::uint8_t { U = 0, V, W, RX, RY, RZ, PSI, N, VY, VZ, MX, MY, MZ, Q, kFull };

using FullMatrix = Eigen::Matrix<double, kFull, kFull>;
using FullVector = Eigen::Matrix<double, kFull, 1>;
using Vec3 = Eigen::Vector3d;
using Mat3 = Eigen::Matrix3d;

Mat3 skew(const Vec3& w)
{
    Mat3 m;
    m << 0.0, -w.z(), w.y(), w.z(), 0.0, -w.x(), -w.y(), w.x(), 0.0;
    return m;
}

// The states used: the displacements of an end, then their forces.
struct Plan
{
    std::array<int, kFull> state{};
    int count = 0; // states
    int dofs = 0;  // per end
};

Plan planFor(const BeamData& beam, bool planar)
{
    Plan plan;
    const auto take = [&plan](std::initializer_list<int> states) {
        for (const int s : states) {
            plan.state.at(static_cast<std::size_t>(plan.count++)) = s;
        }
    };
    if (planar) {
        take({U, V, RZ, N, VY, MZ});
    } else if (beam.warpingStiffness > 0.0) {
        take({U, V, W, RX, RY, RZ, PSI, N, VY, VZ, MX, MY, MZ, Q});
    } else {
        take({U, V, W, RX, RY, RZ, N, VY, VZ, MX, MY, MZ});
    }
    plan.dofs = plan.count / 2;
    return plan;
}

// 1 / (1 + N / G As): the Engesser factor of one plane (1 without shear).
double engesser(double n, double shear)
{
    return shear > 0.0 ? 1.0 / (1.0 + n / shear) : 1.0;
}

// Feature 008d (design 008d §5.5, ADR 0008): Wagner's term of the torsion,
// N r0^2 + beta_y My - beta_z Mz (the integral of sigma r^2 about the shear
// centre, with sigma = N / A - Mz y / Iz + My z / Iy). With the slope of the
// centroid, (theta + e kappa_x), the energy of N is 1/2 N |theta|^2 +
// N kappa_x (ez theta_z + ey theta_y) + 1/2 N r0^2 kappa_x^2.
double wagner(const BeamData& beam, const ReferencePoint& r)
{
    return r.n * beam.polarRadius2 + beam.monosymmetryY * r.my - beam.monosymmetryZ * r.mz;
}

FullMatrix systemMatrix(const BeamData& beam, const ReferencePoint& r, double height = 0.0)
{
    FullMatrix a = FullMatrix::Zero();
    const Mat3 omega = skew(beam.curvature);
    const double ey = beam.shearCentre.x();
    const double ez = beam.shearCentre.y();
    const bool warping = beam.warpingStiffness > 0.0;
    const double n = r.n;
    // Feature 008d: the Saint-Venant stiffness with Wagner's term.
    const double torsional = beam.torsional + wagner(beam, r);
    // The twist rate as a row over the states. Feature 008d: without
    // warping, kappa_x = (T_S - N (ez theta_z + ey theta_y)) / (GJ + K), the
    // conjugate of T_S in the complementary energy of design 008d §5.5.
    Eigen::Matrix<double, 1, kFull> twist = Eigen::Matrix<double, 1, kFull>::Zero();
    if (warping) {
        twist(PSI) = 1.0;
    } else if (torsional != 0.0) {
        // Negative beyond the torsional buckling of a bar without warping
        // (the count marks it unstable, longestPart); only zero is left out.
        twist(MX) = 1.0 / torsional;
        twist(VY) = ez / torsional;
        twist(VZ) = -ey / torsional;
        twist(RZ) = -n * ez / torsional;
        twist(RY) = -n * ey / torsional;
    }
    // u' = -Omega x u - e1 x theta + Gamma.
    a.block<3, 3>(U, U) = -omega;
    const double fy = engesser(n, beam.shearY);
    const double fz = engesser(n, beam.shearZ);
    a(V, RZ) += fy;
    a(W, RY) -= fz;
    a(U, N) += 1.0 / beam.axial;
    if (beam.shearY > 0.0) {
        a(V, VY) += fy / beam.shearY;
    }
    if (beam.shearZ > 0.0) {
        a(W, VZ) += fz / beam.shearZ;
    }
    a.row(V) += ez * twist;
    a.row(W) -= ey * twist;
    // theta' = -Omega x theta + K.
    a.block<3, 3>(RX, RX) = -omega;
    a.row(RX) += twist;
    if (beam.bendingY > 0.0) {
        a(RY, MY) += 1.0 / beam.bendingY;
    }
    if (beam.bendingZ > 0.0) {
        a(RZ, MZ) += 1.0 / beam.bendingZ;
    }
    // Warping: psi' = Q / E Iw, Q' = (G J + K) psi + N (ez theta_z + ey
    // theta_y) - T_S (feature 008d).
    if (warping) {
        a(PSI, Q) = 1.0 / beam.warpingStiffness;
        a(Q, PSI) = torsional;
        a(Q, MX) = -1.0;
        a(Q, VY) = -ez;
        a(Q, VZ) = ey;
        a(Q, RZ) += n * ez;
        a(Q, RY) += n * ey;
    }
    // F' = -Omega x F - q, with the foundation q = -k u.
    a.block<3, 3>(N, N) = -omega;
    a(N, U) += beam.foundation(0);
    a(VY, V) += beam.foundation(1);
    a(VZ, W) += beam.foundation(2);
    // M' = -Omega x M - e1 x F - m, with m_x = -k_t rx.
    a.block<3, 3>(MX, MX) = -omega;
    a(MY, VZ) += 1.0;
    a(MZ, VY) -= 1.0;
    a(MX, RX) += beam.foundation(3);
    // Feature 008d (design 008d §5.5): the load at a height d above the
    // shear centre, lambda (q . d) on the twist, like a torsional
    // foundation (negative where it destabilizes).
    a(MX, RX) += height;
    // Second order: My' -= N sl_w, Mz' += N sl_v (design 008c §5.1).
    if (n != 0.0) {
        a(MZ, RZ) += n * fy;
        a(MY, RY) += n * fz;
        if (beam.shearY > 0.0) {
            a(MZ, VY) += n * fy / beam.shearY;
        }
        if (beam.shearZ > 0.0) {
            a(MY, VZ) -= n * fz / beam.shearZ;
        }
        // Feature 008d: the twist about the shear centre in the slope of
        // the centroid, ez kappa_x and -ey kappa_x.
        a.row(MZ) += n * ez * twist;
        a.row(MY) += n * ey * twist;
    }
    // Feature 008d (design 008d §5.5, ADR 0008): the bending moments in the
    // twisted section, m = M - theta x M0 on its twist: My + theta_x Mz0 and
    // Mz - theta_x My0 in the curvatures, and their conjugate in the torque.
    if (r.my != 0.0 || r.mz != 0.0) {
        if (beam.bendingY > 0.0) {
            a(RY, RX) += r.mz / beam.bendingY;
            a(MX, MY) -= r.mz / beam.bendingY;
            a(MX, RX) -= r.mz * r.mz / beam.bendingY;
        }
        if (beam.bendingZ > 0.0) {
            a(RZ, RX) -= r.my / beam.bendingZ;
            a(MX, MZ) += r.my / beam.bendingZ;
            a(MX, RX) -= r.my * r.my / beam.bendingZ;
        }
    }
    // Feature 008d (T21b, ADR 0008 addendum): the reference torque, Mx (v'
    // w'' - w' v''). With the semitangential moments of the states, p = m -
    // theta x M0 / 2 (ADR 0008), the curvatures are C^-1 (p - theta x M0 /
    // 2): with M0 = T e1, My - tau theta_z and Mz + tau theta_y, tau = T / 2,
    // and the conjugate in the moment rows (H = (My + theta_x Mz0 - tau
    // theta_z)^2 / 2 EIy + (Mz - theta_x My0 + tau theta_y)^2 / 2 EIz; M' =
    // -dH/dtheta). For My0 and Mz0 the twist terms carry the whole moment:
    // the other half is the end term of stateBarMatrices.
    if (r.t != 0.0) {
        const double tau = 0.5 * r.t;
        if (beam.bendingY > 0.0) {
            const double c = tau / beam.bendingY;
            a(RY, RZ) -= c;
            a(MZ, MY) += c;
            a(MZ, RZ) -= tau * c;
            a(MZ, RX) += r.mz * c;
            a(MX, RZ) += r.mz * c;
        }
        if (beam.bendingZ > 0.0) {
            const double c = tau / beam.bendingZ;
            a(RZ, RY) += c;
            a(MY, MZ) -= c;
            a(MY, RY) -= tau * c;
            a(MY, RX) += r.my * c;
            a(MX, RY) += r.my * c;
        }
    }
    // Feature 008d (T23, ADR 0008 addendum): the moment states are m - P
    // theta, P the matrix of the terms above (rows: (0, Mz, -My), (0, 0,
    // T / 2), (0, -T / 2, 0)). Along an arc their derivative in the moving
    // frame brings the curvature: P (Omega x M0) + P Omega - Omega P on
    // theta in the moment rows (zero on a straight bar).
    if (!beam.curvature.isZero() && (r.t != 0.0 || r.my != 0.0 || r.mz != 0.0)) {
        const auto pOf = [](const Eigen::Vector3d& m) {
            Mat3 p = Mat3::Zero();
            p(0, 1) = m(2);
            p(0, 2) = -m(1);
            p(1, 2) = 0.5 * m(0);
            p(2, 1) = -0.5 * m(0);
            return p;
        };
        const Eigen::Vector3d m0(r.t, r.my, r.mz);
        const Mat3 p = pOf(m0);
        a.block<3, 3>(MX, RX) += pOf(beam.curvature.cross(m0)) + p * omega - omega * p;
    }
    return a;
}

// The load terms b of s' = A s + b for a distributed load (qx, qy, qz, mx)
// as SpanPiece holds it (mx about the shear centre).
FullVector distributedTerms(const BeamData& beam, const Eigen::Vector4d& q)
{
    FullVector b = FullVector::Zero();
    const double ey = beam.shearCentre.x();
    const double ez = beam.shearCentre.y();
    b(N) = -q(0);
    b(VY) = -q(1);
    b(VZ) = -q(2);
    // About the centroid: the lever of the 008 pieces comes off.
    b(MX) = -(q(3) - (ez * q(1) - ey * q(2)));
    return b;
}

// The jump of the state at a concentrated load (forces after = before - P).
FullVector pointJump(const BeamData& beam, const SpanPoint& point)
{
    FullVector j = FullVector::Zero();
    const double ey = beam.shearCentre.x();
    const double ez = beam.shearCentre.y();
    j(N) = -point.force.x();
    j(VY) = -point.force.y();
    j(VZ) = -point.force.z();
    j(MX) = -(point.moment.x() - (ez * point.force.y() - ey * point.force.z()));
    j(MY) = -point.moment.y();
    j(MZ) = -point.moment.z();
    return j;
}

// The rate of the fastest homogeneous solution, for the subdivision.
double growthRate(const BeamData& beam)
{
    double rate = beam.curvature.norm();
    if (beam.warpingStiffness > 0.0) {
        rate = std::max(rate, std::sqrt(beam.torsional / beam.warpingStiffness));
    }
    const auto quartic = [](double k, double ei) {
        return k > 0.0 && ei > 0.0 ? std::pow(k / (4.0 * ei), 0.25) : 0.0;
    };
    const auto square = [](double k, double stiffness) {
        return k > 0.0 && stiffness > 0.0 ? std::sqrt(k / stiffness) : 0.0;
    };
    rate = std::max({rate, quartic(beam.foundation(1), beam.bendingZ),
                     quartic(beam.foundation(2), beam.bendingY),
                     square(beam.foundation(0), beam.axial),
                     square(beam.foundation(3), beam.torsional)});
    if (beam.warpingStiffness > 0.0) {
        rate = std::max(rate, quartic(beam.foundation(3), beam.warpingStiffness));
    }
    // Feature 008c: a tension N grows as e^(sqrt(N / EI) x).
    const double tension = referenceOf(beam).scaled(-1.0).largestCompression();
    rate = std::max({rate, square(tension, beam.bendingY), square(tension, beam.bendingZ)});
    return rate;
}

using PartMatrix = StateBarMatrix;

// Feature 008d (REQ-008d-13, design 008d §5.5): the load-height stiffness
// at x, lambda (q . d) of the distributed loads covering it, d from the
// shear centre to their point; and of a concentrated load, lambda (P . d).
double heightOf(const BeamData& beam, const Eigen::Vector2d& point, const Vec3& force)
{
    const Eigen::Vector2d d = point - beam.shearCentre;
    return referenceOf(beam).loadFactor * (force.y() * d.x() + force.z() * d.y());
}

double heightAt(const BeamData& beam, const SpanLoads& loads, double x)
{
    if (referenceOf(beam).loadFactor == 0.0) {
        return 0.0;
    }
    double k = 0.0;
    for (const SpanPiece& piece : loads.pieces) {
        const double span = piece.to - piece.from;
        if (!(span > 0.0) || x < piece.from || x > piece.to) {
            continue;
        }
        const Eigen::Vector4d q =
            piece.start + (piece.end - piece.start) * ((x - piece.from) / span);
        k += heightOf(beam, piece.point, q.head<3>());
    }
    return k;
}

// Whether the loads have height terms at all.
bool hasHeight(const BeamData& beam, const SpanLoads& loads)
{
    if (referenceOf(beam).loadFactor == 0.0) {
        return false;
    }
    const auto off = [&beam](const Eigen::Vector2d& point) { return point != beam.shearCentre; };
    return std::ranges::any_of(loads.pieces, [&](const SpanPiece& p) { return off(p.point); }) ||
           std::ranges::any_of(loads.points, [&](const SpanPoint& p) { return off(p.point); });
}

bool fixedOn(const BeamData& beam, const SpanLoads& loads, double x0, double x1,
             const Eigen::Vector2d& point, Vec3& c0, Vec3& d0);
using PartVector = StateBarVector;

// The two Gauss points of [0, 1] and the weight of the commutator of the
// fourth-order Magnus integrator (design 008c §5.2).
constexpr double kGaussLow = 0.5 - 0.28867513459481288225; // 1/2 - sqrt(3)/6
constexpr double kGaussHigh = 0.5 + 0.28867513459481288225;
constexpr double kMagnusCommutator = 0.14433756729740644113; // sqrt(3)/12

// Transfer over one part: s(end) = T s(start) + p, in physical units, with
// the loads and jumps inside the part. With an axial force that varies
// along the bar (design 008c §5.2), A follows it and each segment is
// integrated by the fourth-order Magnus method.
class Part
{
public:
    Part(const BeamData& beam, const Plan& plan, double length, double from, double to,
         const SpanLoads& loads)
        : m_plan(plan)
        , m_beam(beam)
        , m_loads(loads)
        , m_length(length)
        , m_varying(referenceOf(beam).varies() || hasHeight(beam, loads))
        , m_omega(skew(beam.curvature))
    {
        const int n = plan.count;
        const double middle = (from + to) / 2.0;
        m_a = reduced(systemMatrix(beam, referenceAt(beam, loads, middle, length),
                                   heightAt(beam, loads, middle)));
        // Scales: lengths h, rotations 1, warping 1/h, forces S/h^2,
        // moments S/h, Q S, with S a bending stiffness.
        const double h = to - from;
        double s = std::max(beam.bendingY, beam.bendingZ);
        if (!(s > 0.0)) {
            s = beam.axial * h * h;
        }
        m_scale = StateBarVector::Ones(n);
        for (int i = 0; i < n; ++i) {
            switch (state(i)) {
            case U:
            case V:
            case W:
                m_scale(i) = h;
                break;
            case PSI:
                m_scale(i) = 1.0 / h;
                break;
            case N:
            case VY:
            case VZ:
                m_scale(i) = s / (h * h);
                break;
            case MX:
            case MY:
            case MZ:
                m_scale(i) = s / h;
                break;
            case Q:
                m_scale(i) = s;
                break;
            default:
                break;
            }
        }
        m_t = StateMatrix::Identity(n, n);
        m_p = StateBarVector::Zero(n);
    }

    // Integrates over [x0, x1] inside the part with the load b0 + b1 (x - x0)
    // and, when `fixed`, a force fixed in space whose local components are
    // c0 + d0 (x - x0) at x0 and turn with the frame.
    void segment(double x0, double x1, const FullVector& b0, const FullVector& b1,
                 const BeamData& beam, const SpanLoads& loads)
    {
        const int n = m_plan.count;
        const double l = x1 - x0;
        if (!(l > 0.0)) {
            return;
        }
        const auto generator = [&](const StateMatrix& a) {
            StateMatrix m = StateMatrix::Zero(n + 2, n + 2);
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    m(i, j) = l * a(i, j) * m_scale(j) / m_scale(i);
                }
                m(i, n) = l * l * b1(state(i)) / m_scale(i);
                m(i, n + 1) = l * b0(state(i)) / m_scale(i);
            }
            m(n, n + 1) = 1.0;
            return m;
        };
        const StateMatrix e = matrixExponential(magnus(x0, l, generator));
        StateMatrix t(n, n);
        StateBarVector p(n);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                t(i, j) = m_scale(i) * e(i, j) / m_scale(j);
            }
            p(i) = m_scale(i) * e(i, n + 1);
        }
        // The forces fixed in space, grouped by their point in the section
        // (feature 008d): each group adds its torque about the centroid.
        const auto& fixed = loads.fixedPieces;
        for (std::size_t k = 0; k < fixed.size(); ++k) {
            const Eigen::Vector2d& point = fixed[k].point;
            const bool seen =
                std::any_of(fixed.begin(), fixed.begin() + static_cast<std::ptrdiff_t>(k),
                            [&point](const SpanFixedPiece& other) { return other.point == point; });
            Vec3 c0;
            Vec3 d0;
            if (!seen && fixedOn(beam, loads, x0, x1, point, c0, d0)) {
                p += fixedParticular(x0, l, c0, d0, point);
            }
        }
        m_t = t * m_t;
        m_p = t * m_p + p;
    }

    // The particular solution of a force fixed in space over a segment of
    // length l: c' = -Omega x c + d, d' = -Omega x d, F' gets -c (augmented
    // states c and l d, design 008b §5.3).
    [[nodiscard]] StateBarVector fixedParticular(double x0, double l, const Vec3& c0,
                                                 const Vec3& d0, const Eigen::Vector2d& point) const
    {
        const int n = m_plan.count;
        const auto generator = [&](const StateMatrix& a) {
            StateMatrix m = StateMatrix::Zero(n + 6, n + 6);
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    m(i, j) = l * a(i, j) * m_scale(j) / m_scale(i);
                }
                const int s = state(i);
                if (s == N || s == VY || s == VZ) {
                    m(i, n + (s - N)) = -l / m_scale(i);
                }
                // REQ-008d-13: Mx' gets -(P x c)_x = -(Py cz - Pz cy).
                if (s == MX) {
                    m(i, n + 1) = l * point.y() / m_scale(i);
                    m(i, n + 2) = -l * point.x() / m_scale(i);
                }
            }
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    m(n + i, n + j) = -l * m_omega(i, j);
                    m(n + 3 + i, n + 3 + j) = -l * m_omega(i, j);
                }
                m(n + i, n + 3 + i) = 1.0;
            }
            return m;
        };
        const StateMatrix e = matrixExponential(magnus(x0, l, generator));
        Eigen::Matrix<double, 6, 1> start;
        start << c0, l * d0;
        StateBarVector p(n);
        for (int i = 0; i < n; ++i) {
            p(i) = m_scale(i) * (e.block(i, n, 1, 6) * start)(0);
        }
        return p;
    }

    [[nodiscard]] const StateMatrix& transfer() const { return m_t; }
    [[nodiscard]] const StateBarVector& particular() const { return m_p; }

    void jump(const FullVector& j)
    {
        for (int i = 0; i < m_plan.count; ++i) {
            m_p(i) += j(state(i));
        }
    }

    // Feature 008d: a concentrated load at a height, Mx += k theta_x.
    void kick(double k)
    {
        int mx = -1;
        int rx = -1;
        for (int i = 0; i < m_plan.count; ++i) {
            mx = state(i) == MX ? i : mx;
            rx = state(i) == RX ? i : rx;
        }
        if (k == 0.0 || mx < 0 || rx < 0) {
            return;
        }
        m_t.row(mx) += k * m_t.row(rx);
        m_p(mx) += k * m_p(rx);
    }

    // P = K d + r: the forces the nodes apply on the part.
    void stiffness(PartMatrix& k, PartVector& r) const
    {
        const Eigen::Index d = m_plan.dofs;
        const StateMatrix tdd = m_t.topLeftCorner(d, d);
        const StateMatrix tdf = m_t.topRightCorner(d, d);
        const StateMatrix tfd = m_t.bottomLeftCorner(d, d);
        const StateMatrix tff = m_t.bottomRightCorner(d, d);
        const Eigen::PartialPivLU<StateMatrix> lu(tdf);
        const StateMatrix a = lu.solve(tdd);                         // Tdf^-1 Tdd
        const StateMatrix b = lu.solve(StateMatrix::Identity(d, d)); // Tdf^-1
        const StateBarVector pd = lu.solve(m_p.head(d));             // Tdf^-1 pd
        k = PartMatrix::Zero(2 * d, 2 * d);
        k.topLeftCorner(d, d) = a;
        k.topRightCorner(d, d) = -b;
        k.bottomLeftCorner(d, d) = tfd - tff * a;
        k.bottomRightCorner(d, d) = tff * b;
        r = PartVector::Zero(2 * d);
        r.head(d) = pd;
        r.tail(d) = m_p.tail(d) - tff * pd;
        const PartMatrix symmetric = (k + k.transpose()) / 2.0;
        k = symmetric;
    }

private:
    [[nodiscard]] int state(int i) const { return m_plan.state.at(static_cast<std::size_t>(i)); }

    // The states used, from the full matrix.
    [[nodiscard]] StateMatrix reduced(const FullMatrix& full) const
    {
        const int n = m_plan.count;
        StateMatrix a(n, n);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                a(i, j) = full(state(i), state(j));
            }
        }
        return a;
    }

    // The exponent over [x0, x0 + l]: the generator of the constant A, or
    // with a varying axial force the fourth-order Magnus exponent
    // (G1 + G2) / 2 + sqrt(3) / 12 [G2, G1], G at the two Gauss points.
    template <typename Generator>
    [[nodiscard]] StateMatrix magnus(double x0, double l, const Generator& generator) const
    {
        if (!m_varying) {
            return generator(m_a);
        }
        const auto at = [&](double xi) {
            const double x = x0 + xi * l;
            return reduced(systemMatrix(m_beam, referenceAt(m_beam, m_loads, x, m_length),
                                        heightAt(m_beam, m_loads, x)));
        };
        const StateMatrix g1 = generator(at(kGaussLow));
        const StateMatrix g2 = generator(at(kGaussHigh));
        return 0.5 * (g1 + g2) + kMagnusCommutator * (g2 * g1 - g1 * g2);
    }

    const Plan& m_plan;
    const BeamData& m_beam;
    const SpanLoads& m_loads;
    double m_length;
    bool m_varying;
    Mat3 m_omega;
    StateMatrix m_a;
    StateBarVector m_scale;
    StateMatrix m_t;
    StateBarVector m_p;
};

// The forces fixed in space on [x0, x1] acting at `point` of the section:
// their local components c0 + d0 (x - x0) at x0, turned from the start of
// the bar; false when there are none.
bool fixedOn(const BeamData& beam, const SpanLoads& loads, double x0, double x1,
             const Eigen::Vector2d& point, Vec3& c0, Vec3& d0)
{
    c0.setZero();
    d0.setZero();
    bool any = false;
    const double middle = (x0 + x1) / 2.0;
    for (const SpanFixedPiece& piece : loads.fixedPieces) {
        const double span = piece.to - piece.from;
        if (!(span > 0.0) || middle < piece.from || middle > piece.to || piece.point != point) {
            continue;
        }
        const Vec3 slope = (piece.end - piece.start) / span;
        c0 += piece.start + slope * (x0 - piece.from);
        d0 += slope;
        any = true;
    }
    if (any) {
        // Local components at x0 of a vector fixed in space: e^(-x0 Omega x).
        StateMatrix turn = StateMatrix::Zero(3, 3);
        turn.topLeftCorner(3, 3) = -x0 * skew(beam.curvature);
        const StateMatrix rotation = matrixExponential(turn);
        const Mat3 r = rotation.topLeftCorner(3, 3);
        c0 = r * c0;
        d0 = r * d0;
    }
    return any;
}

// The loads at x as b0 + b1 (x - x0) on [x0, x1]: the pieces that cover it.
void loadsOn(const BeamData& beam, const SpanLoads& loads, double x0, double x1, FullVector& b0,
             FullVector& b1)
{
    b0 = FullVector::Zero();
    b1 = FullVector::Zero();
    b0(U) = loads.imposedStrain;
    b0(RY) = loads.curvatureY;
    b0(RZ) = loads.curvatureZ;
    const double middle = (x0 + x1) / 2.0;
    for (const SpanPiece& piece : loads.pieces) {
        const double span = piece.to - piece.from;
        if (!(span > 0.0) || middle < piece.from || middle > piece.to) {
            continue;
        }
        const Eigen::Vector4d slope = (piece.end - piece.start) / span;
        const Eigen::Vector4d at = piece.start + slope * (x0 - piece.from);
        b0 += distributedTerms(beam, at);
        b1 += distributedTerms(beam, slope);
    }
}

} // namespace

// Feature 008d (design 008d §5.4): the internal forces at s of a straight
// bar from those at its start and its span loads, by statics (the state
// equations with no curvature): F' = -q, Mx' = -(mx about the centroid),
// My' = Fz, Mz' = -Fy, the concentrated loads as jumps.
ReferencePoint straightStatics(const BeamData& beam, const SpanLoads& loads,
                               const ReferencePoint& start, double s, double factor)
{
    const double ey = beam.shearCentre.x();
    const double ez = beam.shearCentre.y();
    Eigen::Vector3d f(start.n, start.vy, start.vz);
    // The loads times `factor` (1 in the profiles, lambda in the engine).
    double mx = start.t - (ez * start.vy - ey * start.vz); // about the centroid
    double my = start.my + f.z() * s;
    double mz = start.mz - f.y() * s;
    for (const SpanPiece& piece : loads.pieces) {
        const double span = piece.to - piece.from;
        const double e = std::min(piece.to, s);
        if (!(span > 0.0) || !(e > piece.from)) {
            continue;
        }
        const double l = e - piece.from;
        const double d = s - piece.from;
        const Eigen::Vector4d c0 = factor * piece.start;
        const Eigen::Vector4d c1 = factor * (piece.end - piece.start) / span;
        // The integral of q, and of (s - x) q, over [from, e].
        const Eigen::Vector4d once = c0 * l + c1 * (l * l / 2.0);
        const Eigen::Vector4d moment =
            c0 * (d * l - l * l / 2.0) + c1 * (d * l * l / 2.0 - l * l * l / 3.0);
        f -= once.head<3>();
        mx -= once(3) - (ez * once(1) - ey * once(2));
        my -= moment(2);
        mz += moment(1);
    }
    for (const SpanPoint& point : loads.points) {
        if (!(point.at < s)) {
            continue;
        }
        const double d = s - point.at;
        const Eigen::Vector3d force = factor * point.force;
        const Eigen::Vector3d moment = factor * point.moment;
        f -= force;
        mx -= moment.x() - (ez * force.y() - ey * force.z());
        my -= moment.y() + force.z() * d;
        mz -= moment.z() - force.y() * d;
    }
    return {.n = f.x(),
            .vy = f.y(),
            .vz = f.z(),
            .t = mx + ez * f.y() - ey * f.z(),
            .my = my,
            .mz = mz};
}

ReferencePoint referenceAt(const BeamData& beam, const SpanLoads& loads, double s, double length)
{
    const ReferenceProfile& reference = referenceOf(beam);
    if (reference.statics) {
        const ReferencePoint exact =
            straightStatics(beam, loads, reference.start, s, reference.loadFactor);
        // Feature 008d (T24b): the samples' difference from the statics, linear
        // within each part (zero when the samples are the statics).
        const int parts = reference.parts();
        const int part = std::clamp(static_cast<int>(s / length * parts), 0, parts - 1);
        const auto points = ReferenceProfile::gaussPoints(part, parts, length);
        const std::size_t k = 2 * static_cast<std::size_t>(part);
        const ReferencePoint first = reference.values.at(k) - reference.base.at(k);
        const ReferencePoint second = reference.values.at(k + 1) - reference.base.at(k + 1);
        const double w = (s - points[0]) / (points[1] - points[0]);
        return exact + first * (1.0 - w) + second * w;
    }
    return reference.at(s, length);
}

ReferencePoint spatialForces(const BeamData& beam, const BarState& state,
                             const ReferencePoint& reference)
{
    // P(M0) theta: rows (0, Mz, -My), (0, 0, T / 2), (0, -T / 2, 0).
    const Vec3& theta = state.rotation;
    const Vec3 shift(reference.mz * theta.y() - reference.my * theta.z(),
                     0.5 * reference.t * theta.z(), -0.5 * reference.t * theta.y());
    const Vec3 moment = state.moment + shift;
    const double ey = beam.shearCentre.x();
    const double ez = beam.shearCentre.y();
    return {.n = state.force.x(),
            .vy = state.force.y(),
            .vz = state.force.z(),
            .t = moment.x() + ez * state.force.y() - ey * state.force.z(),
            .my = moment.y(),
            .mz = moment.z()};
}

int stateDofs(const BeamData& beam, bool planar)
{
    return planFor(beam, planar).dofs;
}

namespace {

// The smallest positive of two stiffnesses (0 when neither is).
double smallestPositive(double a, double b)
{
    if (a > 0.0 && b > 0.0) {
        return std::min(a, b);
    }
    return std::max(a, b);
}

// Feature 008c (design 008c §5.1): the longest part whose clamped critical
// load (Engesser) stays above kMargin times the largest compression c, so
// that no part buckles on its own; 0 when the compression needs none, and
// < 0 when it is beyond the reach of any part (|N| near G As).
constexpr double kCriticalMargin = 4.0;
constexpr double kVaryingChange = 5e-5;
constexpr int kMaxVaryingPieces = 256;
double longestPart(const BeamData& beam, const SpanLoads& loads)
{
    // Feature 008d (design 008d §5.6): without warping, the torsion buckles
    // along the whole bar at once when Wagner's term cancels G J, whatever
    // the length of the parts: beyond the reach of any part.
    const ReferenceProfile& reference = referenceOf(beam);
    if (!(beam.warpingStiffness > 0.0) && beam.torsional > 0.0) {
        for (int i = 0; i < reference.count; ++i) {
            if (!(beam.torsional + wagner(beam, reference.values.at(static_cast<std::size_t>(i))) >
                  0.0)) {
                return -1.0;
            }
        }
    }
    const double ei = smallestPositive(beam.bendingY, beam.bendingZ);
    double longest = 0.0; // 0: no limit
    const auto limit = [&longest](double l) { longest = longest > 0.0 ? std::min(longest, l) : l; };
    // Feature 008d: with warping, the part clamped at both ends buckles in
    // torsion when G J + 4 pi^2 E Iw / l^2 + K = 0, and in lateral torsion
    // when (2 pi / l)^2 EI (G J + 4 pi^2 E Iw / l^2) = M^2: kept above
    // kCriticalMargin times the largest -K and |M|.
    double softening = 0.0; // the largest -K
    double moment = 0.0;    // the largest |M|
    double torque = 0.0;    // the largest |T|
    for (int i = 0; i < reference.count; ++i) {
        const ReferencePoint& r = reference.values.at(static_cast<std::size_t>(i));
        softening = std::max(softening, -wagner(beam, r));
        moment = std::max({moment, std::abs(r.my), std::abs(r.mz)});
        torque = std::max(torque, std::abs(r.t));
    }
    const double twoPi = 2.0 * std::numbers::pi;
    const double eiw = beam.warpingStiffness;
    if (eiw > 0.0 && kCriticalMargin * softening > beam.torsional) {
        limit(twoPi * std::sqrt(eiw / (kCriticalMargin * softening - beam.torsional)));
    }
    if (moment > 0.0 && ei > 0.0 && (beam.torsional > 0.0 || eiw > 0.0)) {
        // x = (2 pi / l)^2: EI GJ x + EI EIw x^2 = (margin M)^2.
        const double target = std::pow(kCriticalMargin * moment, 2) / ei;
        const double gj = beam.torsional;
        const double x =
            eiw > 0.0 ? (-gj + std::sqrt(gj * gj + 4.0 * eiw * target)) / (2.0 * eiw) : target / gj;
        limit(twoPi / std::sqrt(x));
    }
    // Feature 008d (T21b): the torque, below the Greenhill load of the part
    // clamped at both ends (2.86 pi EI / l; 2 pi EI / l kept).
    if (torque > 0.0 && ei > 0.0) {
        limit(twoPi * ei / (kCriticalMargin * torque));
    }
    // Feature 008d: the loads at a height, a torsional foundation of
    // lambda (q . d) and point springs of lambda (P . d), destabilizing where
    // negative: the part clamped at both ends kept stable with the same
    // margin, x = (2 pi / l)^2 with E Iw x^2 + G' x >= margin |k| for the
    // foundation, and 4 G' / l or 192 E Iw / l^3 >= margin |k| for a spring.
    double foundation = 0.0;
    for (const SpanPiece& piece : loads.pieces) {
        foundation = std::max({foundation, -heightOf(beam, piece.point, piece.start.head<3>()),
                               -heightOf(beam, piece.point, piece.end.head<3>())});
    }
    double spring = 0.0;
    for (const SpanPoint& point : loads.points) {
        spring = std::max(spring, -heightOf(beam, point.point, point.force));
    }
    const double torsion = beam.torsional - softening; // G', the least along the bar
    if (foundation > 0.0) {
        const double target = kCriticalMargin * foundation;
        if (eiw > 0.0) {
            const double x =
                (-torsion + std::sqrt(torsion * torsion + 4.0 * eiw * target)) / (2.0 * eiw);
            limit(twoPi / std::sqrt(x));
        } else if (torsion > 0.0) {
            limit(twoPi * std::sqrt(torsion / target));
        }
    }
    if (spring > 0.0) {
        const double target = kCriticalMargin * spring;
        double allowed = 0.0;
        if (torsion > 0.0) {
            allowed = 4.0 * torsion / target;
        }
        if (eiw > 0.0) {
            allowed = std::max(allowed, std::cbrt(192.0 * eiw / target));
        }
        if (allowed > 0.0) {
            limit(allowed);
        }
    }
    const double c = reference.largestCompression();
    if (!(c > 0.0) || !(ei > 0.0)) {
        return longest;
    }
    const double ga = smallestPositive(beam.shearY, beam.shearZ);
    double margin = kCriticalMargin;
    if (ga > 0.0 && margin * c >= ga) {
        margin = 1.25;
        if (margin * c >= ga) {
            return -1.0;
        }
    }
    const double shear = ga > 0.0 ? 1.0 - margin * c / ga : 1.0;
    limit(twoPi * std::sqrt(ei * shear / (margin * c)));
    return longest;
}

// At most kMaxParts parts: a compression that needs more is beyond the reach
// of the bar (the count marks it unstable), and the parts stay bounded when
// a search tries a far load factor.
constexpr int kMaxParts = 8192; // kMaxAxialParts x kMaxVaryingPieces

// ceil(count) for a part count, saturated above kMaxParts (also for NaN):
// never an overflow of int.
int partsFor(double count)
{
    return count <= kMaxParts ? std::max(1, static_cast<int>(std::ceil(count))) : kMaxParts + 1;
}

// Whether the compression needs more parts than kMaxParts.
bool beyondParts(const BeamData& beam, double length, const SpanLoads& loads)
{
    const double longest = longestPart(beam, loads);
    return longest > 0.0 && partsFor(length / longest) > kMaxParts;
}

int partCount(const BeamData& beam, double length, const SpanLoads& loads)
{
    int parts = partsFor(growthRate(beam) * length / kGrowthPerPart);
    const double longest = longestPart(beam, loads);
    if (longest > 0.0) {
        parts = std::max(parts, partsFor(length / longest));
    }
    // The parts of the profile of N are split evenly and, when N varies,
    // so that each piece changes (k h)^2 by at most kVaryingChange: the
    // error of the fourth-order Magnus integrator (design 008c §5.2,
    // calibrated on Greenhill's column for 1e-8).
    // Feature 008d: the same for the moments, by the change of M h /
    // sqrt(EI GJ) (the scale of their flexural-torsional terms).
    const ReferenceProfile& reference = referenceOf(beam);
    const int profile = reference.varies() ? reference.parts() : 1;
    const double ei = smallestPositive(beam.bendingY, beam.bendingZ);
    if (reference.varies() && ei > 0.0) {
        const double h = length / profile;
        const double scale = beam.torsional > 0.0 ? std::sqrt(ei * beam.torsional) : 0.0;
        int pieces = 1;
        for (int part = 0; part < profile; ++part) {
            const ReferencePoint& low = reference.values.at(2 * static_cast<std::size_t>(part));
            const ReferencePoint& high =
                reference.values.at(2 * static_cast<std::size_t>(part) + 1);
            const double step = 1.0 / (kGaussHigh - kGaussLow);
            double change = std::abs(high.n - low.n) * step * h * h / ei;
            if (scale > 0.0) {
                const double moment =
                    std::max(std::abs(high.my - low.my), std::abs(high.mz - low.mz)) * step;
                change = std::max(change, moment * h / scale);
            }
            pieces = std::max(pieces, partsFor(std::cbrt(change / kVaryingChange)));
        }
        parts = std::max(parts, profile * std::min(pieces, kMaxVaryingPieces));
    }
    parts = std::min(parts, kMaxParts);
    parts = profile * ((parts + profile - 1) / profile);
    return parts;
}

// Integrates the part from `from` up to `until`, segment by segment between
// the breakpoints of the loads, found one after the other (no storage:
// compute() must not allocate). The concentrated loads at the start of a
// segment act after it, those at the start of the bar when `barStart`, and
// those at `until` (the end of the bar included) when `jumpAtUntil`.
void walk(Part& part, const BeamData& beam, const SpanLoads& loads, double length, double from,
          double until, bool barStart, bool jumpAtUntil)
{
    const auto nextBreak = [&loads, until](double x) {
        double next = until;
        const auto consider = [&next, x](double b) {
            if (b > x && b < next) {
                next = b;
            }
        };
        for (const SpanPiece& piece : loads.pieces) {
            consider(piece.from);
            consider(piece.to);
        }
        for (const SpanPoint& point : loads.points) {
            consider(point.at);
        }
        for (const SpanFixedPiece& piece : loads.fixedPieces) {
            consider(piece.from);
            consider(piece.to);
        }
        return next;
    };
    // Whether a concentrated load acts at x, the start of a segment.
    const auto startsAt = [barStart, from, length](const SpanPoint& point, double x) {
        return point.at < length && (point.at == x || (barStart && x == from && point.at <= 0.0));
    };
    for (double x0 = from; x0 < until;) {
        const double x1 = nextBreak(x0);
        for (const SpanPoint& point : loads.points) {
            if (startsAt(point, x0)) {
                part.jump(pointJump(beam, point));
                part.kick(heightOf(beam, point.point, point.force));
            }
        }
        FullVector b0;
        FullVector b1;
        loadsOn(beam, loads, x0, x1, b0, b1);
        part.segment(x0, x1, b0, b1, beam, loads);
        x0 = x1;
    }
    if (!jumpAtUntil) {
        return;
    }
    for (const SpanPoint& point : loads.points) {
        const bool atEnd = until >= length && point.at >= length;
        // A load at `from` already acted when the walk had a segment.
        const bool acted = until > from && startsAt(point, from);
        const bool here = atEnd || (until <= from ? startsAt(point, from) : point.at == until);
        if (here && !acted && (atEnd || point.at < length)) {
            part.jump(pointJump(beam, point));
            part.kick(heightOf(beam, point.point, point.force));
        }
    }
}

} // namespace

StateBarMatrices stateBarMatrices(const BeamData& beam, double length, const SpanLoads& loads,
                                  bool planar)
{
    const Plan plan = planFor(beam, planar);
    const Eigen::Index d = plan.dofs;
    const int parts = partCount(beam, length, loads);
    const bool second = referenceOf(beam).active();
    int negative =
        longestPart(beam, loads) < 0.0 || beyondParts(beam, length, loads) ? kUnstableBar : 0;

    PartMatrix total;
    PartVector totalLoads;
    for (int k = 0; k < parts; ++k) {
        const double from = length * k / parts;
        const double to = k + 1 == parts ? length : length * (k + 1) / parts;
        Part part(beam, plan, length, from, to, loads);
        walk(part, beam, loads, length, from, to, k == 0, k + 1 == parts);
        PartMatrix kp;
        PartVector rp;
        part.stiffness(kp, rp);
        if (k == 0) {
            total = kp;
            totalLoads = rp;
            continue;
        }
        // Condense the junction between the parts so far and this part.
        const PartMatrix kjj = total.bottomRightCorner(d, d) + kp.topLeftCorner(d, d);
        const PartVector rj = totalLoads.tail(d) + rp.head(d);
        PartMatrix coupling = PartMatrix::Zero(2 * d, d); // (start, next) x junction
        coupling.topRows(d) = total.topRightCorner(d, d);
        coupling.bottomRows(d) = kp.bottomLeftCorner(d, d);
        PartMatrix solved; // junction x (start, next)
        PartVector junction;
        if (second) {
            // May be indefinite: its negative eigenvalues count the
            // buckling loads of the clamped bar below N (design 008c §5.4).
            negative += negativeEigenvalues(kjj);
            const Eigen::PartialPivLU<PartMatrix> lu(kjj);
            solved = lu.solve(coupling.transpose());
            junction = lu.solve(rj);
        } else {
            const Eigen::LDLT<PartMatrix> ldlt(kjj);
            solved = ldlt.solve(coupling.transpose());
            junction = ldlt.solve(rj);
        }
        PartMatrix next = PartMatrix::Zero(2 * d, 2 * d);
        next.topLeftCorner(d, d) = total.topLeftCorner(d, d);
        next.bottomRightCorner(d, d) = kp.bottomRightCorner(d, d);
        next -= coupling * solved;
        PartVector nextLoads(2 * d);
        nextLoads.head(d) = totalLoads.head(d);
        nextLoads.tail(d) = rp.tail(d);
        nextLoads -= coupling * junction;
        total = (next + next.transpose()) / 2.0;
        totalLoads = nextLoads;
    }
    // ADR 0008 (T20): the moments at the ends of the bar act on the joint
    // as semitangential moments. The second variation of the bar gives
    // them, at each end, half a coupling too many between the twist and the
    // bending rotations: (t w^T + w t^T) / 2 with w = t x M; it cancels
    // between collinear bars and parts, not at a joint of bars at an angle
    // (Argyris's right-angle frame). Removed here, at the two ends.
    if (second && !planar) {
        for (int end = 0; end < 2; ++end) {
            const ReferencePoint m = referenceAt(beam, loads, end == 0 ? 0.0 : length, length);
            const double sign = end == 0 ? 1.0 : -1.0;
            const Eigen::Index rx = end * d + 3;
            // w = x x (T, My, Mz) = (0, -Mz, My) in the local axes.
            total(rx, rx + 1) -= sign * 0.5 * m.mz;
            total(rx + 1, rx) -= sign * 0.5 * m.mz;
            total(rx, rx + 2) += sign * 0.5 * m.my;
            total(rx + 2, rx) += sign * 0.5 * m.my;
        }
    }
    StateBarMatrices out;
    out.stiffness = total;
    out.loads = -totalLoads; // equivalent nodal loads
    out.negativePivots = negative;
    return out;
}

int negativeEigenvalues(const StateBarMatrix& matrix)
{
    StateBarMatrix a = (matrix + matrix.transpose()) / 2.0;
    const Eigen::Index n = a.rows();
    const double size = a.norm();
    if (n == 0 || !(size > 0.0)) {
        return 0;
    }
    constexpr int kSweeps = 50;
    for (int sweep = 0; sweep < kSweeps; ++sweep) {
        double off = 0.0;
        for (Eigen::Index p = 0; p < n; ++p) {
            for (Eigen::Index q = p + 1; q < n; ++q) {
                off += a(p, q) * a(p, q);
            }
        }
        if (std::sqrt(off) <= 1e-15 * size) {
            break;
        }
        for (Eigen::Index p = 0; p < n; ++p) {
            for (Eigen::Index q = p + 1; q < n; ++q) {
                if (a(p, q) == 0.0) {
                    continue;
                }
                // The rotation that zeroes a(p, q) (Golub and Van Loan 8.5).
                const double theta = (a(q, q) - a(p, p)) / (2.0 * a(p, q));
                const double t = (theta >= 0.0 ? 1.0 : -1.0) /
                                 (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0);
                const double s = t * c;
                for (Eigen::Index k = 0; k < n; ++k) {
                    const double akp = a(k, p);
                    const double akq = a(k, q);
                    a(k, p) = c * akp - s * akq;
                    a(k, q) = s * akp + c * akq;
                }
                for (Eigen::Index k = 0; k < n; ++k) {
                    const double apk = a(p, k);
                    const double aqk = a(q, k);
                    a(p, k) = c * apk - s * aqk;
                    a(q, k) = s * apk + c * aqk;
                }
            }
        }
    }
    return static_cast<int>((a.diagonal().array() < 0.0).count());
}

std::vector<StateBarMatrix> partStiffnesses(const BeamData& beam, double length, bool planar,
                                            const SpanLoads& loads)
{
    const Plan plan = planFor(beam, planar);
    const int parts = partCount(beam, length, loads);
    std::vector<StateBarMatrix> out(static_cast<std::size_t>(parts));
    for (int k = 0; k < parts; ++k) {
        const double from = length * k / parts;
        const double to = k + 1 == parts ? length : length * (k + 1) / parts;
        // The loads matter for their heights only (the stiffness).
        Part part(beam, plan, length, from, to, loads);
        walk(part, beam, loads, length, from, to, k == 0, k + 1 == parts);
        PartVector r;
        part.stiffness(out[static_cast<std::size_t>(k)], r);
    }
    return out;
}

void statesAt(const BeamData& beam, double length, const SpanLoads& loads, bool planar,
              const StateBarVector& ends, std::span<const double> positions, Side side,
              std::span<const double> given, std::span<BarState> results)
{
    const Plan plan = planFor(beam, planar);
    const Eigen::Index d = plan.dofs;
    const int parts = partCount(beam, length, loads);
    const auto bound = [length, parts](int k) { return k >= parts ? length : length * k / parts; };
    // The stiffness of every part, then the displacements of the junctions.
    std::vector<PartMatrix> stiffness(static_cast<std::size_t>(parts));
    std::vector<PartVector> forces(static_cast<std::size_t>(parts));
    for (int k = 0; k < parts; ++k) {
        Part part(beam, plan, length, bound(k), bound(k + 1), loads);
        walk(part, beam, loads, length, bound(k), bound(k + 1), k == 0, k + 1 == parts);
        part.stiffness(stiffness[static_cast<std::size_t>(k)], forces[static_cast<std::size_t>(k)]);
    }
    const Eigen::Index inner = (parts - 1) * d;
    Eigen::VectorXd junctions = Eigen::VectorXd::Zero(std::max<Eigen::Index>(inner, 1));
    if (inner > 0 && static_cast<Eigen::Index>(given.size()) == inner) {
        for (Eigen::Index i = 0; i < inner; ++i) {
            junctions(i) = given[static_cast<std::size_t>(i)];
        }
    } else if (inner > 0) {
        Eigen::MatrixXd kii = Eigen::MatrixXd::Zero(inner, inner);
        Eigen::VectorXd rhs = Eigen::VectorXd::Zero(inner);
        for (int k = 0; k < parts; ++k) {
            const PartMatrix& kp = stiffness[static_cast<std::size_t>(k)];
            const PartVector& rp = forces[static_cast<std::size_t>(k)];
            // Junction k - 1 at the start of the part, junction k at its end.
            const Eigen::Index a = (k - 1) * d;
            const Eigen::Index b = k * d;
            const bool startInner = k > 0;
            const bool endInner = k + 1 < parts;
            if (startInner) {
                kii.block(a, a, d, d) += kp.topLeftCorner(d, d);
                rhs.segment(a, d) -= rp.head(d);
                if (!endInner) {
                    rhs.segment(a, d) -= kp.topRightCorner(d, d) * ends.tail(d);
                }
            }
            if (endInner) {
                kii.block(b, b, d, d) += kp.bottomRightCorner(d, d);
                rhs.segment(b, d) -= rp.tail(d);
                if (startInner) {
                    kii.block(a, b, d, d) += kp.topRightCorner(d, d);
                    kii.block(b, a, d, d) += kp.bottomLeftCorner(d, d);
                } else {
                    rhs.segment(b, d) -= kp.bottomLeftCorner(d, d) * ends.head(d);
                }
            }
        }
        // With an axial force the junctions may be indefinite.
        junctions = referenceOf(beam).active() ? Eigen::VectorXd(kii.partialPivLu().solve(rhs))
                                               : Eigen::VectorXd(kii.ldlt().solve(rhs));
    }
    // Feature 008d (T24b): the parts and the junctions once, then each
    // position.
    for (std::size_t q = 0; q < positions.size(); ++q) {
        const double s = positions[q];
        // The part that holds s and the displacements of its ends.
        const double x = std::clamp(s, 0.0, length);
        int k = std::min(parts - 1, static_cast<int>(x / length * parts));
        while (k > 0 && x < bound(k)) {
            --k;
        }
        while (k + 1 < parts && x >= bound(k + 1)) {
            ++k;
        }
        StateBarVector dk(2 * d);
        if (k == 0) {
            dk.head(d) = ends.head(d);
        } else {
            dk.head(d) = junctions.segment((k - 1) * d, d);
        }
        if (k + 1 == parts) {
            dk.tail(d) = ends.tail(d);
        } else {
            dk.tail(d) = junctions.segment(k * d, d);
        }
        const auto at = static_cast<std::size_t>(k);
        const PartVector p = stiffness[at] * dk + forces[at];
        // The state at the start of the part: its displacements, and the
        // internal forces, minus what the start node applies.
        StateBarVector start(2 * d);
        start.head(d) = dk.head(d);
        start.tail(d) = -p.head(d);
        Part part(beam, plan, length, bound(k), bound(k + 1), loads);
        walk(part, beam, loads, length, bound(k), x, k == 0, side == Side::After);
        const StateBarVector state = part.transfer() * start + part.particular();
        BarState out;
        for (Eigen::Index i = 0; i < 2 * d; ++i) {
            const double value = state(i);
            switch (plan.state.at(static_cast<std::size_t>(i))) {
            case U:
                out.displacement.x() = value;
                break;
            case V:
                out.displacement.y() = value;
                break;
            case W:
                out.displacement.z() = value;
                break;
            case RX:
                out.rotation.x() = value;
                break;
            case RY:
                out.rotation.y() = value;
                break;
            case RZ:
                out.rotation.z() = value;
                break;
            case PSI:
                out.warping = value;
                break;
            case N:
                out.force.x() = value;
                break;
            case VY:
                out.force.y() = value;
                break;
            case VZ:
                out.force.z() = value;
                break;
            case MX:
                out.moment.x() = value;
                break;
            case MY:
                out.moment.y() = value;
                break;
            case MZ:
                out.moment.z() = value;
                break;
            case Q:
                out.bimoment = -value;
                break;
            default:
                break;
            }
        }
        results[q] = out;
    }
}

BarState stateAt(const BeamData& beam, double length, const SpanLoads& loads, bool planar,
                 const StateBarVector& ends, double s, Side side, std::span<const double> given)
{
    BarState out;
    statesAt(beam, length, loads, planar, ends, std::span<const double>(&s, 1), side, given,
             std::span<BarState>(&out, 1));
    return out;
}

} // namespace fem::core
