#include <fem/core/beam_element.hpp>

#include <Eigen/Cholesky>
#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace fem::core {

namespace {

// The Gauss points of [0, 1], two points.
constexpr double kGaussLow = 0.5 - 0.28867513459481288225; // 1/2 - sqrt(3)/6
constexpr double kGaussHigh = 0.5 + 0.28867513459481288225;

} // namespace

ReferenceProfile ReferenceProfile::constant(double n)
{
    return constant(ReferencePoint{.n = n});
}

ReferenceProfile ReferenceProfile::constant(const ReferencePoint& point)
{
    ReferenceProfile profile;
    profile.count = 1;
    profile.values[0] = point;
    return profile;
}

std::array<double, 2> ReferenceProfile::gaussPoints(int part, int parts, double length)
{
    const double h = length / parts;
    return {(part + kGaussLow) * h, (part + kGaussHigh) * h};
}

ReferencePoint ReferenceProfile::at(double s, double length) const
{
    if (count == 0) {
        return {};
    }
    if (count == 1) {
        return values[0];
    }
    const int n = parts();
    const double h = length / n;
    const int part = std::clamp(static_cast<int>(std::floor(s / h)), 0, n - 1);
    const double xi = s / h - part;
    const ReferencePoint& low = values.at(2 * static_cast<std::size_t>(part));
    const ReferencePoint& high = values.at(2 * static_cast<std::size_t>(part) + 1);
    const double w = (xi - kGaussLow) / (kGaussHigh - kGaussLow);
    return {.n = low.n + (high.n - low.n) * w,
            .vy = low.vy + (high.vy - low.vy) * w,
            .vz = low.vz + (high.vz - low.vz) * w,
            .t = low.t + (high.t - low.t) * w,
            .my = low.my + (high.my - low.my) * w,
            .mz = low.mz + (high.mz - low.mz) * w};
}

double ReferenceProfile::largestCompression() const
{
    double smallest = 0.0;
    if (count == 1) {
        smallest = values[0].n;
    }
    for (int part = 0; count > 1 && part < parts(); ++part) {
        const double low = values.at(2 * static_cast<std::size_t>(part)).n;
        const double high = values.at(2 * static_cast<std::size_t>(part) + 1).n;
        const double slope = (high - low) / (kGaussHigh - kGaussLow);
        smallest = std::min({smallest, low - slope * kGaussLow, low + slope * (1.0 - kGaussLow)});
    }
    return std::max(0.0, -smallest);
}

ReferenceProfile ReferenceProfile::scaled(double factor) const
{
    ReferenceProfile out = *this;
    for (int i = 0; i < count; ++i) {
        out.values.at(static_cast<std::size_t>(i)) =
            values.at(static_cast<std::size_t>(i)) * factor;
    }
    out.loadFactor = loadFactor * factor;
    out.start = start * factor;
    return out;
}

bool ReferenceProfile::axialVaries() const
{
    for (int i = 1; i < count; ++i) {
        if (values.at(static_cast<std::size_t>(i)).n != values[0].n) {
            return true;
        }
    }
    return false;
}

bool ReferenceProfile::varies() const
{
    const ReferencePoint& a = values[0];
    for (int i = 1; i < count; ++i) {
        const ReferencePoint& b = values.at(static_cast<std::size_t>(i));
        if (b.n != a.n || b.vy != a.vy || b.vz != a.vz || b.t != a.t || b.my != a.my ||
            b.mz != a.mz) {
            return true;
        }
    }
    return false;
}

const ReferenceProfile& referenceOf(const BeamData& beam)
{
    static const ReferenceProfile kNone{};
    return beam.reference ? *beam.reference : kNone;
}

namespace {

constexpr double kParallel = 1e-6;  // |x × r| below this: parallel (design 008 §5.2)
constexpr double kSingular = 1e-12; // pivot of the internal dofs, of the largest
constexpr int kMaxInternal = kBeamDofs;

// Local dofs of the two planes of bending, in BeamDof order per end.
constexpr std::array<int, 4> kPlaneXY = {1, 5, 7, 11}; // v1, rz1, v2, rz2
constexpr std::array<int, 4> kPlaneXZ = {2, 4, 8, 10}; // w1, ry1, w2, ry2
// The dofs of a 2D beam among the 12 of 3D.
constexpr std::array<int, 6> kPlanar = {0, 1, 5, 6, 7, 11};

// 4-point Gauss rule on [0, 1].
constexpr std::array<double, 4> kGaussPoints = {0.0694318442029737, 0.3300094782075719,
                                                0.6699905217924281, 0.9305681557970263};
constexpr std::array<double, 4> kGaussWeights = {0.1739274225687269, 0.3260725774312731,
                                                 0.3260725774312731, 0.1739274225687269};

using InternalMatrix =
    Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, 0, kMaxInternal, kMaxInternal>;
using InternalVector = Eigen::Matrix<double, Eigen::Dynamic, 1, 0, kMaxInternal, 1>;
using CouplingMatrix = Eigen::Matrix<double, kBeamDofs, Eigen::Dynamic, 0, kBeamDofs, kMaxInternal>;

// Bending stiffness of one plane for (v1, t1, v2, t2), t = dv/dx
// (Przemieniecki, Theory of Matrix Structural Analysis, 1968, §5.6).
Eigen::Matrix4d planeStiffness(double ei, double length, double phi)
{
    const double l = length;
    const double c = ei / ((1.0 + phi) * l * l * l);
    Eigen::Matrix4d k;
    k << 12.0, 6.0 * l, -12.0, 6.0 * l,                              //
        6.0 * l, (4.0 + phi) * l * l, -6.0 * l, (2.0 - phi) * l * l, //
        -12.0, -6.0 * l, 12.0, -6.0 * l,                             //
        6.0 * l, (2.0 - phi) * l * l, -6.0 * l, (4.0 + phi) * l * l;
    return c * k;
}

// In the x-z plane the rotation ry is -dw/dx: the plane functions apply to
// (w1, -ry1, w2, -ry2).
const Eigen::Vector4d kFlipXZ(1.0, -1.0, 1.0, -1.0);

bool isPlanar(ElementKind kind)
{
    return kind == ElementKind::Beam2D;
}

// Translation of a rigid link: u_p = u_n + rx_n × r = u_n - [r]x rx_n.
Eigen::Matrix3d cross(const Eigen::Vector3d& r)
{
    Eigen::Matrix3d m;
    m << 0.0, -r.z(), r.y(), //
        r.z(), 0.0, -r.x(),  //
        -r.y(), r.x(), 0.0;
    return m;
}

// Global nodal dofs to local dofs at the ends of the flexible length:
// offsets, then the rotation to the principal axes (design 008 §5.4).
BeamMatrix nodeToLocal(const BeamData& beam)
{
    BeamMatrix a = BeamMatrix::Zero();
    const Eigen::Matrix3d toLocal = beam.axes.transpose();
    for (int end = 0; end < 2; ++end) {
        const int o = end * kBeamEndDofs;
        a.block<3, 3>(o, o) = toLocal;
        a.block<3, 3>(o, o + 3) = -toLocal * cross(beam.offsets[static_cast<std::size_t>(end)]);
        a.block<3, 3>(o + 3, o + 3) = toLocal;
    }
    return a;
}

// The connections of design 008 §5.4: internal dofs where a spring or a
// release sits, and the condensation onto the external ones.
class Connections
{
public:
    Connections(const BeamData& beam, ElementKind kind, const BeamMatrix& stiffness,
                const BeamVector& loads)
    {
        for (int j = 0; j < kBeamDofs; ++j) {
            const int component = j % kBeamEndDofs;
            const double spring = beam.springs[static_cast<std::size_t>(j / kBeamEndDofs)]
                                              [static_cast<std::size_t>(component)];
            const bool used = !isPlanar(kind) || component == 0 || component == 1 || component == 5;
            if (used && spring != kRigid) {
                m_internal[static_cast<std::size_t>(j)] = m_count;
                m_spring[static_cast<std::size_t>(m_count)] = spring;
                m_external[static_cast<std::size_t>(m_count)] = j;
                ++m_count;
            }
        }
        m_kei = CouplingMatrix::Zero(kBeamDofs, m_count);
        m_kii = InternalMatrix::Zero(m_count, m_count);
        m_fi = InternalVector::Zero(m_count);
        // The bar on its dofs (external where rigid, internal elsewhere).
        for (int a = 0; a < kBeamDofs; ++a) {
            const int ia = m_internal[static_cast<std::size_t>(a)];
            for (int b = 0; b < kBeamDofs; ++b) {
                const int ib = m_internal[static_cast<std::size_t>(b)];
                const double k = stiffness(a, b);
                if (ia < 0 && ib < 0) {
                    m_kee(a, b) += k;
                } else if (ia < 0) {
                    m_kei(a, ib) += k;
                } else if (ib >= 0) {
                    m_kii(ia, ib) += k;
                }
            }
            (ia < 0 ? m_fe(a) : m_fi(ia)) += loads(a);
        }
        // The springs between each external dof and its internal one.
        for (int i = 0; i < m_count; ++i) {
            const int e = m_external[static_cast<std::size_t>(i)];
            const double s = m_spring[static_cast<std::size_t>(i)];
            m_kee(e, e) += s;
            m_kii(i, i) += s;
            m_kei(e, i) -= s;
        }
        if (m_count > 0) {
            m_ldlt.compute(m_kii);
        }
    }

    [[nodiscard]] bool stable() const
    {
        if (m_count == 0) {
            return true;
        }
        const auto d = m_ldlt.vectorD();
        const double largest =
            std::max(d.cwiseAbs().maxCoeff(), m_kii.diagonal().cwiseAbs().maxCoeff());
        return m_ldlt.info() == Eigen::Success && d.minCoeff() > kSingular * largest;
    }

    // K* = Kee - Kei Kii^-1 Kie and f* = fe - Kei Kii^-1 fi.
    void condense(BeamMatrix& stiffness, BeamVector& loads) const
    {
        stiffness = m_kee;
        loads = m_fe;
        if (m_count == 0) {
            return;
        }
        const CouplingMatrix x = m_ldlt.solve(m_kei.transpose()).transpose();
        stiffness.noalias() -= x * m_kei.transpose();
        loads.noalias() -= x * m_fi;
    }

    // The dofs of the flexible length from the external ones.
    [[nodiscard]] BeamVector flexible(const BeamVector& external) const
    {
        BeamVector d = external;
        if (m_count == 0) {
            return d;
        }
        const InternalVector internal = m_ldlt.solve(m_fi - m_kei.transpose() * external);
        for (int i = 0; i < m_count; ++i) {
            d(m_external[static_cast<std::size_t>(i)]) = internal(i);
        }
        return d;
    }

private:
    int m_count = 0;
    std::array<int, kBeamDofs> m_internal{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    std::array<int, kMaxInternal> m_external{};
    std::array<double, kMaxInternal> m_spring{};
    BeamMatrix m_kee = BeamMatrix::Zero();
    CouplingMatrix m_kei;
    InternalMatrix m_kii;
    BeamVector m_fe = BeamVector::Zero();
    InternalVector m_fi;
    Eigen::LDLT<InternalMatrix> m_ldlt;
};

} // namespace

PlaneShape planeShape(double xi, double length, double phi)
{
    const double x2 = xi * xi;
    const double x3 = x2 * xi;
    const double d = 1.0 + phi;
    PlaneShape s;
    s.displacement << (1.0 - 3.0 * x2 + 2.0 * x3 + phi * (1.0 - xi)) / d,
        length * (xi - 2.0 * x2 + x3 + 0.5 * phi * (xi - x2)) / d,
        (3.0 * x2 - 2.0 * x3 + phi * xi) / d, length * (-x2 + x3 + 0.5 * phi * (x2 - xi)) / d;
    const double r = 6.0 * (x2 - xi) / (length * d);
    s.rotation << r, (1.0 - 4.0 * xi + 3.0 * x2 + phi * (1.0 - xi)) / d, -r,
        (-2.0 * xi + 3.0 * x2 + phi * xi) / d;
    return s;
}

double shearParameter(const BeamData& beam, double length, bool aboutZ)
{
    const double shear = aboutZ ? beam.shearY : beam.shearZ;
    const double bending = aboutZ ? beam.bendingZ : beam.bendingY;
    return shear > 0.0 ? 12.0 * bending / (shear * length * length) : 0.0;
}

Expected<Eigen::Matrix3d> beamAxes(const Eigen::Vector3d& x1, const Eigen::Vector3d& x2,
                                   const std::optional<Eigen::Vector3d>& reference, double roll,
                                   ElementKind kind)
{
    const Eigen::Vector3d delta = x2 - x1;
    const double length = delta.norm();
    if (!(length > 0.0)) {
        return makeError("The bar has zero length.");
    }
    const Eigen::Vector3d x = delta / length;
    Eigen::Vector3d y;
    if (isPlanar(kind)) {
        y = Eigen::Vector3d::UnitZ().cross(x);
        y.z() = 0.0;
        y.normalize();
    } else {
        Eigen::Vector3d r = Eigen::Vector3d::UnitZ();
        if (reference) {
            const double size = reference->norm();
            if (!(size > 0.0) || x.cross(*reference / size).norm() < kParallel) {
                return makeError("The reference vector is parallel to the bar.");
            }
            r = *reference / size;
        } else if (x.cross(r).norm() < kParallel) {
            r = Eigen::Vector3d::UnitX();
        }
        y = (r - r.dot(x) * x).normalized();
        if (roll != 0.0) {
            y = Eigen::AngleAxisd(roll, x) * y;
        }
    }
    Eigen::Matrix3d axes;
    axes.col(0) = x;
    axes.col(1) = y;
    axes.col(2) = x.cross(y);
    return axes;
}

Expected<Arc> arcThrough(const Eigen::Vector3d& p, const Eigen::Vector3d& m,
                         const Eigen::Vector3d& q)
{
    constexpr double kCoincident = 1e-9;
    constexpr double kClosed = 1e-6; // of a full turn
    const Eigen::Vector3d a = m - p;
    const Eigen::Vector3d b = q - p;
    const double scale = std::max({a.norm(), b.norm(), (q - m).norm()});
    if (!(a.norm() > kCoincident * scale) || !((q - m).norm() > kCoincident * scale)) {
        return makeError("the middle node coincides with an end node.");
    }
    if (!(b.norm() > kCoincident * scale)) {
        return makeError("the arc closes on itself.");
    }
    const Eigen::Vector3d n = a.cross(b);
    Arc arc;
    if (n.norm() <= kParallel * a.norm() * b.norm()) {
        // Collinear: a straight bar when m is between the ends.
        const double along = a.dot(b) / b.squaredNorm();
        if (!(along > 0.0) || !(along < 1.0)) {
            return makeError("the middle node is not between the end nodes.");
        }
        arc.length = b.norm();
        arc.startTangent = arc.endTangent = b / b.norm();
        return arc;
    }
    // The circumcentre of p, m, q.
    arc.centre = p + (a.squaredNorm() * b - b.squaredNorm() * a).cross(n) / (2.0 * n.squaredNorm());
    arc.radius = (p - arc.centre).norm();
    arc.normal = n.normalized();
    const Eigen::Vector3d u = p - arc.centre;
    const Eigen::Vector3d v = q - arc.centre;
    double angle = std::atan2(arc.normal.dot(u.cross(v)), u.dot(v));
    if (angle <= 0.0) {
        angle += 2.0 * std::numbers::pi;
    }
    if (angle >= 2.0 * std::numbers::pi * (1.0 - kClosed)) {
        return makeError("the arc closes on itself.");
    }
    arc.length = arc.radius * angle;
    arc.startTangent = arc.normal.cross(u).normalized();
    arc.endTangent = arc.normal.cross(v).normalized();
    return arc;
}

Eigen::Vector3d arcCurvature(const Eigen::Matrix3d& startAxes, const Arc& arc)
{
    if (!(arc.radius > 0.0)) {
        return Eigen::Vector3d::Zero();
    }
    return startAxes.transpose() * arc.normal / arc.radius;
}

Eigen::Matrix3d arcAxesAt(const Eigen::Matrix3d& startAxes, const Eigen::Vector3d& curvature,
                          double s)
{
    const double rate = curvature.norm();
    if (!(rate > 0.0)) {
        return startAxes;
    }
    return startAxes * Eigen::AngleAxisd(rate * s, curvature / rate).toRotationMatrix();
}

double flexibleLength(const BeamData& beam, const Eigen::Vector3d& x1, const Eigen::Vector3d& x2)
{
    return ((x2 + beam.offsets[1]) - (x1 + beam.offsets[0])).norm();
}

BeamMatrix flexibleStiffness(const BeamData& beam, double length)
{
    // On the shear centre (design 008 §5.3).
    BeamMatrix kb = BeamMatrix::Zero();
    const double axial = beam.axial / length;
    const double torsion = beam.torsional / length;
    for (Eigen::Index a = 0; a < 2; ++a) {
        for (Eigen::Index b = 0; b < 2; ++b) {
            const double sign = a == b ? 1.0 : -1.0;
            kb(6 * a, 6 * b) = sign * axial;
            kb(6 * a + 3, 6 * b + 3) = sign * torsion;
        }
    }
    const Eigen::Matrix4d kxy =
        planeStiffness(beam.bendingZ, length, shearParameter(beam, length, true));
    const Eigen::Matrix4d kxz =
        kFlipXZ.asDiagonal() *
        planeStiffness(beam.bendingY, length, shearParameter(beam, length, false)) *
        kFlipXZ.asDiagonal();
    for (std::size_t a = 0; a < 4; ++a) {
        for (std::size_t b = 0; b < 4; ++b) {
            kb(kPlaneXY[a], kPlaneXY[b]) = kxy(static_cast<int>(a), static_cast<int>(b));
            kb(kPlaneXZ[a], kPlaneXZ[b]) = kxz(static_cast<int>(a), static_cast<int>(b));
        }
    }
    // To the centroid: v_S = v - ez rx, w_S = w + ey rx.
    BeamMatrix t = BeamMatrix::Identity();
    for (int end = 0; end < 2; ++end) {
        const int o = end * kBeamEndDofs;
        t(o + 1, o + 3) = -beam.shearCentre.y();
        t(o + 2, o + 3) = beam.shearCentre.x();
    }
    BeamMatrix k = t.transpose() * kb * t;

    // REQ-008-15: the consistent matrix of the foundation, on the centroid.
    if (!beam.foundation.isZero()) {
        const double phiY = shearParameter(beam, length, true);
        const double phiZ = shearParameter(beam, length, false);
        for (std::size_t g = 0; g < kGaussPoints.size(); ++g) {
            const double xi = kGaussPoints[g];
            const double w = kGaussWeights[g] * length;
            BeamVector nu = BeamVector::Zero();
            BeamVector nv = BeamVector::Zero();
            BeamVector nw = BeamVector::Zero();
            BeamVector nr = BeamVector::Zero();
            nu(0) = nr(3) = 1.0 - xi;
            nu(6) = nr(9) = xi;
            const Eigen::Vector4d sy = planeShape(xi, length, phiY).displacement;
            const Eigen::Vector4d sz =
                kFlipXZ.cwiseProduct(planeShape(xi, length, phiZ).displacement);
            for (std::size_t a = 0; a < 4; ++a) {
                nv(kPlaneXY[a]) = sy(static_cast<int>(a));
                nw(kPlaneXZ[a]) = sz(static_cast<int>(a));
            }
            k.noalias() += w * (beam.foundation(0) * nu * nu.transpose() +
                                beam.foundation(1) * nv * nv.transpose() +
                                beam.foundation(2) * nw * nw.transpose() +
                                beam.foundation(3) * nr * nr.transpose());
        }
    }
    return k;
}

bool hasStableConnections(const BeamData& beam, double length, ElementKind kind)
{
    const Connections connections(beam, kind, flexibleStiffness(beam, length), BeamVector::Zero());
    return connections.stable();
}

BeamEnds beamEnds(const BeamData& beam, ElementKind kind, const Eigen::Vector3d& x1,
                  const Eigen::Vector3d& x2, const BeamVector& nodeValues)
{
    const double length = flexibleLength(beam, x1, x2);
    const BeamMatrix k = flexibleStiffness(beam, length);
    const Connections connections(beam, kind, k, beam.fixedEndLoads);
    BeamEnds ends;
    ends.displacements = connections.flexible(nodeToLocal(beam) * nodeValues);
    ends.forces = k * ends.displacements - beam.fixedEndLoads;
    return ends;
}

BeamElement::BeamElement(ElementKind kind, std::span<const BeamData> beams)
    : m_kind(kind)
    , m_beams(beams)
{}

DofMask BeamElement::dofsAt(ElementType /*type*/, int /*localNode*/) const
{
    const DofMask plane = mask(DofType::U) | mask(DofType::V) | mask(DofType::RZ);
    return isPlanar(m_kind) ? plane
                            : static_cast<DofMask>(plane | mask(DofType::W) | mask(DofType::RX) |
                                                   mask(DofType::RY));
}

Expected<void> BeamElement::compute(const ElementNodes& nodes, ElementIndex element,
                                    Eigen::Ref<Eigen::MatrixXd> matrix,
                                    Eigen::Ref<Eigen::VectorXd> vector) const
{
    if (nodes.type != ElementType::Line2) {
        return makeError("A beam element must be a 2-node line.");
    }
    const BeamData& beam = m_beams[static_cast<std::size_t>(element)];
    const double length = flexibleLength(beam, nodes.x[0], nodes.x[1]);
    if (!(length > 0.0)) {
        return makeError("The bar has zero length.");
    }
    BeamMatrix k;
    BeamVector f;
    const Connections connections(beam, m_kind, flexibleStiffness(beam, length),
                                  beam.fixedEndLoads);
    connections.condense(k, f);
    const BeamMatrix a = nodeToLocal(beam);
    const BeamMatrix kg = a.transpose() * k * a;
    const BeamVector fg = a.transpose() * f;
    if (isPlanar(m_kind)) {
        for (std::size_t i = 0; i < kPlanar.size(); ++i) {
            vector(static_cast<int>(i)) = fg(kPlanar[i]);
            for (std::size_t j = 0; j < kPlanar.size(); ++j) {
                matrix(static_cast<int>(i), static_cast<int>(j)) = kg(kPlanar[i], kPlanar[j]);
            }
        }
    } else {
        matrix = kg;
        vector = fg;
    }
    return {};
}

} // namespace fem::core
