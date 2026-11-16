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

#include <fem/core/beam_state.hpp>

#include <Eigen/Cholesky>
#include <Eigen/LU>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <unsupported/Eigen/MatrixFunctions>
#include <vector>

namespace fem::core {

StateMatrix matrixExponential(const StateMatrix& a)
{
    // ADR 0006: the only use of the module, so that it can be replaced.
    return a.exp();
}

namespace {

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

FullMatrix systemMatrix(const BeamData& beam)
{
    FullMatrix a = FullMatrix::Zero();
    const Mat3 omega = skew(beam.curvature);
    const double ey = beam.shearCentre.x();
    const double ez = beam.shearCentre.y();
    const bool warping = beam.warpingStiffness > 0.0;
    // The twist rate as a row over the states.
    Eigen::Matrix<double, 1, kFull> twist = Eigen::Matrix<double, 1, kFull>::Zero();
    if (warping) {
        twist(PSI) = 1.0;
    } else if (beam.torsional > 0.0) {
        twist(MX) = 1.0 / beam.torsional;
        twist(VY) = ez / beam.torsional;
        twist(VZ) = -ey / beam.torsional;
    }
    // u' = -Omega x u - e1 x theta + Gamma.
    a.block<3, 3>(U, U) = -omega;
    a(V, RZ) += 1.0;
    a(W, RY) -= 1.0;
    a(U, N) += 1.0 / beam.axial;
    if (beam.shearY > 0.0) {
        a(V, VY) += 1.0 / beam.shearY;
    }
    if (beam.shearZ > 0.0) {
        a(W, VZ) += 1.0 / beam.shearZ;
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
    // Warping: psi' = Q / E Iw, Q' = G J psi - T_S.
    if (warping) {
        a(PSI, Q) = 1.0 / beam.warpingStiffness;
        a(Q, PSI) = beam.torsional;
        a(Q, MX) = -1.0;
        a(Q, VY) = -ez;
        a(Q, VZ) = ey;
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
    j(Q) = -point.bimoment;
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
    return rate;
}

using PartMatrix = StateBarMatrix;
using PartVector = StateBarVector;

// Transfer over one part: s(end) = T s(start) + p, in physical units, with
// the loads and jumps inside the part.
class Part
{
public:
    Part(const BeamData& beam, const Plan& plan, const FullMatrix& full, double from, double to)
        : m_plan(plan)
        , m_omega(skew(beam.curvature))
    {
        const int n = plan.count;
        m_a = StateMatrix::Zero(n, n);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                m_a(i, j) = full(state(i), state(j));
            }
        }
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
    void segment(double x0, double x1, const FullVector& b0, const FullVector& b1, bool fixed,
                 const Vec3& c0, const Vec3& d0)
    {
        const int n = m_plan.count;
        const double l = x1 - x0;
        if (!(l > 0.0)) {
            return;
        }
        StateMatrix m = StateMatrix::Zero(n + 2, n + 2);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                m(i, j) = l * m_a(i, j) * m_scale(j) / m_scale(i);
            }
            m(i, n) = l * l * b1(state(i)) / m_scale(i);
            m(i, n + 1) = l * b0(state(i)) / m_scale(i);
        }
        m(n, n + 1) = 1.0;
        const StateMatrix e = matrixExponential(m);
        StateMatrix t(n, n);
        StateBarVector p(n);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                t(i, j) = m_scale(i) * e(i, j) / m_scale(j);
            }
            p(i) = m_scale(i) * e(i, n + 1);
        }
        if (fixed) {
            p += fixedParticular(l, c0, d0);
        }
        m_t = t * m_t;
        m_p = t * m_p + p;
    }

    // The particular solution of a force fixed in space over a segment of
    // length l: c' = -Omega x c + d, d' = -Omega x d, F' gets -c (augmented
    // states c and l d, design 008b §5.3).
    [[nodiscard]] StateBarVector fixedParticular(double l, const Vec3& c0, const Vec3& d0) const
    {
        const int n = m_plan.count;
        StateMatrix m = StateMatrix::Zero(n + 6, n + 6);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                m(i, j) = l * m_a(i, j) * m_scale(j) / m_scale(i);
            }
            const int s = state(i);
            if (s == N || s == VY || s == VZ) {
                m(i, n + (s - N)) = -l / m_scale(i);
            }
        }
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                m(n + i, n + j) = -l * m_omega(i, j);
                m(n + 3 + i, n + 3 + j) = -l * m_omega(i, j);
            }
            m(n + i, n + 3 + i) = 1.0;
        }
        const StateMatrix e = matrixExponential(m);
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

    const Plan& m_plan;
    Mat3 m_omega;
    StateMatrix m_a;
    StateBarVector m_scale;
    StateMatrix m_t;
    StateBarVector m_p;
};

// The forces fixed in space on [x0, x1]: their local components c0 + d0 (x - x0)
// at x0, turned from the start of the bar; false when there are none.
bool fixedOn(const BeamData& beam, const SpanLoads& loads, double x0, double x1, Vec3& c0, Vec3& d0)
{
    c0.setZero();
    d0.setZero();
    bool any = false;
    const double middle = (x0 + x1) / 2.0;
    for (const SpanFixedPiece& piece : loads.fixedPieces) {
        const double span = piece.to - piece.from;
        if (!(span > 0.0) || middle < piece.from || middle > piece.to) {
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

int stateDofs(const BeamData& beam, bool planar)
{
    return planFor(beam, planar).dofs;
}

namespace {

int partCount(const BeamData& beam, double length)
{
    return std::max(1, static_cast<int>(std::ceil(growthRate(beam) * length / 2.0)));
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
            }
        }
        FullVector b0;
        FullVector b1;
        loadsOn(beam, loads, x0, x1, b0, b1);
        Vec3 c0;
        Vec3 d0;
        const bool fixed = fixedOn(beam, loads, x0, x1, c0, d0);
        part.segment(x0, x1, b0, b1, fixed, c0, d0);
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
        }
    }
}

} // namespace

StateBarMatrices stateBarMatrices(const BeamData& beam, double length, const SpanLoads& loads,
                                  bool planar)
{
    const Plan plan = planFor(beam, planar);
    const FullMatrix full = systemMatrix(beam);
    const Eigen::Index d = plan.dofs;
    const int parts = partCount(beam, length);

    PartMatrix total;
    PartVector totalLoads;
    for (int k = 0; k < parts; ++k) {
        const double from = length * k / parts;
        const double to = k + 1 == parts ? length : length * (k + 1) / parts;
        Part part(beam, plan, full, from, to);
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
        const Eigen::LDLT<PartMatrix> ldlt(kjj);
        const PartMatrix solved = ldlt.solve(coupling.transpose()); // junction x (start, next)
        PartMatrix next = PartMatrix::Zero(2 * d, 2 * d);
        next.topLeftCorner(d, d) = total.topLeftCorner(d, d);
        next.bottomRightCorner(d, d) = kp.bottomRightCorner(d, d);
        next -= coupling * solved;
        PartVector nextLoads(2 * d);
        nextLoads.head(d) = totalLoads.head(d);
        nextLoads.tail(d) = rp.tail(d);
        nextLoads -= coupling * ldlt.solve(rj);
        total = (next + next.transpose()) / 2.0;
        totalLoads = nextLoads;
    }
    StateBarMatrices out;
    out.stiffness = total;
    out.loads = -totalLoads; // equivalent nodal loads
    return out;
}

BarState stateAt(const BeamData& beam, double length, const SpanLoads& loads, bool planar,
                 const StateBarVector& ends, double s, Side side)
{
    const Plan plan = planFor(beam, planar);
    const FullMatrix full = systemMatrix(beam);
    const Eigen::Index d = plan.dofs;
    const int parts = partCount(beam, length);
    const auto bound = [length, parts](int k) { return k >= parts ? length : length * k / parts; };
    // The stiffness of every part, then the displacements of the junctions.
    std::vector<PartMatrix> stiffness(static_cast<std::size_t>(parts));
    std::vector<PartVector> forces(static_cast<std::size_t>(parts));
    for (int k = 0; k < parts; ++k) {
        Part part(beam, plan, full, bound(k), bound(k + 1));
        walk(part, beam, loads, length, bound(k), bound(k + 1), k == 0, k + 1 == parts);
        part.stiffness(stiffness[static_cast<std::size_t>(k)], forces[static_cast<std::size_t>(k)]);
    }
    const Eigen::Index inner = (parts - 1) * d;
    Eigen::VectorXd junctions = Eigen::VectorXd::Zero(std::max<Eigen::Index>(inner, 1));
    if (inner > 0) {
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
        junctions = kii.ldlt().solve(rhs);
    }
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
    Part part(beam, plan, full, bound(k), bound(k + 1));
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
    return out;
}

} // namespace fem::core
