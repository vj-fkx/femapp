// REQ-008b-14: the 3-node isoparametric curved beam (design 008b §5.5).
// Generalised strains at the centroid, in the local principal axes at each
// point (Lambda columns x, y, z; t the tangent):
//   Gamma = Lambda^T (u' + t x theta),  kappa = Lambda^T theta',
// with u and theta quadratic in global components. The section law is the
// one of the exact engine (shear centre included); the normal and shear
// terms are integrated with 2 points and the curvature terms with 3
// (Stolarski and Belytschko, 1983; Prathap, 1993), free of shear and
// membrane locking.

#include <fem/core/beam_iso.hpp>
#include <fem/core/shape_functions.hpp>

#include <Eigen/Cholesky>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace fem::core {

namespace {

constexpr int kNodeDofs = 6;
constexpr int kDofs = 18;
constexpr double kPenalty = 1e6;    // Euler-Bernoulli: G As = kPenalty EI / l^2
constexpr double kSingular = 1e-12; // pivot of the internal dofs, of the largest
constexpr int kMaxInternal = 12;

using Mat18 = Eigen::Matrix<double, kDofs, kDofs>;
using Vec18 = Eigen::Matrix<double, kDofs, 1>;
using Mat6 = Eigen::Matrix<double, 6, 6>;
using Vec6 = Eigen::Matrix<double, 6, 1>;
using Strain = Eigen::Matrix<double, 6, kDofs>;
using Coupling = Eigen::Matrix<double, kDofs, Eigen::Dynamic, 0, kDofs, kMaxInternal>;
using Internal =
    Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, 0, kMaxInternal, kMaxInternal>;
using InternalVector = Eigen::Matrix<double, Eigen::Dynamic, 1, 0, kMaxInternal, 1>;

constexpr std::array<double, 3> kGauss3 = {-0.7745966692414834, 0.0, 0.7745966692414834};
constexpr std::array<double, 3> kWeights3 = {5.0 / 9.0, 8.0 / 9.0, 5.0 / 9.0};
constexpr std::array<double, 2> kGauss2 = {-std::numbers::inv_sqrt3, std::numbers::inv_sqrt3};
constexpr std::array<double, 2> kWeights2 = {1.0, 1.0};
constexpr std::array<double, 8> kGauss8 = {
    -0.9602898564975363, -0.7966664774136267, -0.5255324099163290, -0.1834346424956498,
    0.1834346424956498,  0.5255324099163290,  0.7966664774136267,  0.9602898564975363};
constexpr std::array<double, 8> kWeights8 = {
    0.1012285362903763, 0.2223810344533745, 0.3137066458778873, 0.3626837833783620,
    0.3626837833783620, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763};
constexpr std::array<int, 3> kPlanar = {0, 1, 5};

// The first dof of a node in the element vector.
constexpr Eigen::Index at(int node)
{
    return Eigen::Index{kNodeDofs} * node;
}

bool planarKind(ElementKind kind)
{
    return kind == ElementKind::Beam2D;
}

Eigen::Matrix3d cross(const Eigen::Vector3d& r)
{
    Eigen::Matrix3d m;
    m << 0.0, -r.z(), r.y(), r.z(), 0.0, -r.x(), -r.y(), r.x(), 0.0;
    return m;
}

struct Shape
{
    std::array<double, 3> n{};
    std::array<double, 3> dn{}; // d/dxi
};

Shape shapeAt(double xi)
{
    ShapeValues values;
    evaluateShape(ElementType::Line3, {xi, 0.0, 0.0}, values);
    Shape s;
    for (std::size_t i = 0; i < 3; ++i) {
        s.n.at(i) = values.n.at(i);
        s.dn.at(i) = values.dn.at(i)[0];
    }
    return s;
}

double jacobian(const IsoNodes& x, double xi)
{
    const Shape s = shapeAt(xi);
    Eigen::Vector3d d = Eigen::Vector3d::Zero();
    for (std::size_t i = 0; i < 3; ++i) {
        d += s.dn.at(i) * x.at(i);
    }
    return d.norm();
}

// The arc length from xi = -1 to xi.
double arcLength(const IsoNodes& x, double xi)
{
    double s = 0.0;
    const double half = (xi + 1.0) / 2.0;
    for (std::size_t g = 0; g < kGauss8.size(); ++g) {
        s += kWeights8.at(g) * half * jacobian(x, -1.0 + half * (kGauss8.at(g) + 1.0));
    }
    return s;
}

struct Point
{
    Shape shape;
    Eigen::Vector3d x = Eigen::Vector3d::Zero();
    Eigen::Vector3d t = Eigen::Vector3d::UnitX();
    double j = 0.0;
    Eigen::Matrix3d axes = Eigen::Matrix3d::Identity();
};

Expected<Point> pointAt(const IsoNodes& x, const IsoBeamData& data, ElementKind kind, double xi)
{
    Point p;
    p.shape = shapeAt(xi);
    Eigen::Vector3d d = Eigen::Vector3d::Zero();
    for (std::size_t i = 0; i < 3; ++i) {
        p.x += p.shape.n.at(i) * x.at(i);
        d += p.shape.dn.at(i) * x.at(i);
    }
    p.j = d.norm();
    // The derivative is linear in xi: the curve folds back where it turns
    // against the one at the middle.
    Eigen::Vector3d middle = Eigen::Vector3d::Zero();
    const Shape centre = shapeAt(0.0);
    for (std::size_t i = 0; i < 3; ++i) {
        middle += centre.dn.at(i) * x.at(i);
    }
    if (!(p.j > 0.0) || !(d.dot(middle) > 0.0)) {
        return makeError("the curve of the element folds on itself.");
    }
    p.t = d / p.j;
    auto axes = beamAxes(p.x, p.x + p.t, data.reference, data.roll, kind);
    if (!axes) {
        return tl::unexpected(axes.error());
    }
    p.axes = *axes;
    return p;
}

// The section law at the centroid (design 008b §5.3, as the exact engine).
Mat6 constitutive(const BeamData& b, double length)
{
    const double gay = b.shearY > 0.0 ? b.shearY : kPenalty * b.bendingZ / (length * length);
    const double gaz = b.shearZ > 0.0 ? b.shearZ : kPenalty * b.bendingY / (length * length);
    Vec6 stiffness;
    stiffness << b.axial, gay, gaz, b.torsional, b.bendingY, b.bendingZ;
    Mat6 t = Mat6::Identity();
    const double ey = b.shearCentre.x();
    const double ez = b.shearCentre.y();
    t(1, 3) = -ez; // shear strain at the shear centre
    t(2, 3) = ey;
    return t.transpose() * stiffness.asDiagonal() * t;
}

Strain strainAt(const Point& p)
{
    Strain b = Strain::Zero();
    const Eigen::Matrix3d lt = p.axes.transpose();
    for (int i = 0; i < 3; ++i) {
        const auto k = static_cast<std::size_t>(i);
        const double ds = p.shape.dn.at(k) / p.j;
        const Eigen::Index o = at(i);
        b.block<3, 3>(0, o) = ds * lt;
        b.block<3, 3>(0, o + 3) = p.shape.n.at(k) * lt * cross(p.t);
        b.block<3, 3>(3, o + 3) = ds * lt;
    }
    return b;
}

struct Flexible
{
    Mat18 k = Mat18::Zero();
    Vec18 f = Vec18::Zero(); // equivalent nodal loads
};

// The flexible curve on its own, global components at its three points.
Expected<Flexible> flexible(const IsoBeamData& data, ElementKind kind, const IsoNodes& x)
{
    const BeamData& beam = data.beam;
    const double length = arcLength(x, 1.0);
    const Mat6 d = constitutive(beam, length);
    Vec6 imposed = Vec6::Zero();
    imposed << data.loads.imposedStrain, 0.0, 0.0, 0.0, data.loads.curvatureY,
        data.loads.curvatureZ;
    Flexible out;
    // Curvature terms: 3 points.
    for (std::size_t g = 0; g < kGauss3.size(); ++g) {
        auto p = pointAt(x, data, kind, kGauss3.at(g));
        if (!p) {
            return tl::unexpected(p.error());
        }
        const Strain b = strainAt(*p);
        const double w = kWeights3.at(g) * p->j;
        const auto bk = b.bottomRows<3>();
        out.k.noalias() += w * bk.transpose() * d.bottomRightCorner<3, 3>() * bk;
        out.f.noalias() += w * bk.transpose() * d.bottomRightCorner<3, 3>() * imposed.tail<3>();
        // The foundation, on the centroid displacements and the twist.
        if (!beam.foundation.isZero()) {
            const Eigen::Matrix3d kf =
                p->axes * beam.foundation.head<3>().asDiagonal() * p->axes.transpose();
            const Eigen::Matrix3d kt = beam.foundation(3) * p->t * p->t.transpose();
            for (int i = 0; i < 3; ++i) {
                for (int jn = 0; jn < 3; ++jn) {
                    const double nn = p->shape.n.at(static_cast<std::size_t>(i)) *
                                      p->shape.n.at(static_cast<std::size_t>(jn)) * w;
                    out.k.block<3, 3>(at(i), at(jn)) += nn * kf;
                    out.k.block<3, 3>(at(i) + 3, at(jn) + 3) += nn * kt;
                }
            }
        }
    }
    // Normal and shear terms, and their coupling: 2 points.
    for (std::size_t g = 0; g < kGauss2.size(); ++g) {
        auto p = pointAt(x, data, kind, kGauss2.at(g));
        if (!p) {
            return tl::unexpected(p.error());
        }
        const Strain b = strainAt(*p);
        const double w = kWeights2.at(g) * p->j;
        const auto bg = b.topRows<3>();
        const auto bk = b.bottomRows<3>();
        const Eigen::Matrix3d dgg = d.topLeftCorner<3, 3>();
        const Eigen::Matrix3d dgk = d.topRightCorner<3, 3>();
        out.k.noalias() += w * (bg.transpose() * dgg * bg + bg.transpose() * dgk * bk +
                                bk.transpose() * dgk.transpose() * bg);
        out.f.noalias() +=
            w * (bg.transpose() * (dgg * imposed.head<3>() + dgk * imposed.tail<3>()) +
                 bk.transpose() * dgk.transpose() * imposed.head<3>());
    }
    // Distributed loads, per length of the curve.
    for (const IsoLoads::Distributed& load : data.loads.distributed) {
        const double span = load.to - load.from;
        if (!(span > 0.0)) {
            continue;
        }
        const double a = isoParameter(x, load.from);
        const double c = isoParameter(x, load.to);
        for (std::size_t g = 0; g < kGauss8.size(); ++g) {
            const double xi = (a + c) / 2.0 + (c - a) / 2.0 * kGauss8.at(g);
            auto p = pointAt(x, data, kind, xi);
            if (!p) {
                return tl::unexpected(p.error());
            }
            const double fraction = std::clamp((arcLength(x, xi) - load.from) / span, 0.0, 1.0);
            Eigen::Vector3d q = (1.0 - fraction) * load.startValue + fraction * load.endValue;
            if (load.axes == Axes::Local) {
                q = p->axes * q;
            }
            const double m = (1.0 - fraction) * load.startTorque + fraction * load.endTorque;
            const double w = kWeights8.at(g) * (c - a) / 2.0 * p->j;
            for (int i = 0; i < 3; ++i) {
                const double n = p->shape.n.at(static_cast<std::size_t>(i)) * w;
                out.f.segment<3>(at(i)) += n * q;
                out.f.segment<3>(at(i) + 3) += n * m * p->t;
            }
        }
    }
    for (const IsoLoads::Concentrated& load : data.loads.concentrated) {
        auto p = pointAt(x, data, kind, isoParameter(x, load.at));
        if (!p) {
            return tl::unexpected(p.error());
        }
        const Eigen::Vector3d force =
            load.axes == Axes::Local ? Eigen::Vector3d(p->axes * load.force) : load.force;
        const Eigen::Vector3d moment =
            load.axes == Axes::Local ? Eigen::Vector3d(p->axes * load.moment) : load.moment;
        for (int i = 0; i < 3; ++i) {
            const double n = p->shape.n.at(static_cast<std::size_t>(i));
            out.f.segment<3>(at(i)) += n * force;
            out.f.segment<3>(at(i) + 3) += n * moment;
        }
    }
    return out;
}

// The connections of the ends (design 008 §5.4) in the local axes of each
// end, condensed; the external dofs then go to the nodes through the
// offsets. The middle node is external as it is.
class IsoConnections
{
public:
    IsoConnections(const IsoBeamData& data, ElementKind kind, const Point& start, const Point& end,
                   const Flexible& f)
    {
        const BeamData& beam = data.beam;
        // Flexible dofs in the local axes of the ends.
        m_local = Mat18::Identity();
        const std::array<const Point*, 2> ends = {&start, &end};
        for (int e = 0; e < 2; ++e) {
            const Eigen::Matrix3d& axes = ends.at(static_cast<std::size_t>(e))->axes;
            const Eigen::Index o = at(e);
            m_local.block<3, 3>(o, o) = axes;
            m_local.block<3, 3>(o + 3, o + 3) = axes;
        }
        const Mat18 k = m_local.transpose() * f.k * m_local;
        const Vec18 loads = m_local.transpose() * f.f;
        for (int j = 0; j < kDofs; ++j) {
            const int node = j / kNodeDofs;
            const int component = j % kNodeDofs;
            // In the plane only N, Vy and Mz have connections.
            const bool used =
                !planarKind(kind) || component == 0 || component == 1 || component == 5;
            const double spring = node < 2 && used ? beam.springs.at(static_cast<std::size_t>(node))
                                                         .at(static_cast<std::size_t>(component))
                                                   : kRigid;
            if (spring != kRigid) {
                m_internal.at(static_cast<std::size_t>(j)) = m_count;
                m_spring.at(static_cast<std::size_t>(m_count)) = spring;
                m_external.at(static_cast<std::size_t>(m_count)) = j;
                ++m_count;
            }
        }
        m_kei = Coupling::Zero(kDofs, m_count);
        m_kii = Internal::Zero(m_count, m_count);
        m_fi = InternalVector::Zero(m_count);
        for (int a = 0; a < kDofs; ++a) {
            const int ia = m_internal.at(static_cast<std::size_t>(a));
            for (int b = 0; b < kDofs; ++b) {
                const int ib = m_internal.at(static_cast<std::size_t>(b));
                if (ia < 0 && ib < 0) {
                    m_kee(a, b) += k(a, b);
                } else if (ia < 0) {
                    m_kei(a, ib) += k(a, b);
                } else if (ib >= 0) {
                    m_kii(ia, ib) += k(a, b);
                }
            }
            (ia < 0 ? m_fe(a) : m_fi(ia)) += loads(a);
        }
        for (int i = 0; i < m_count; ++i) {
            const int e = m_external.at(static_cast<std::size_t>(i));
            const double s = m_spring.at(static_cast<std::size_t>(i));
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

    void condense(Mat18& k, Vec18& f) const
    {
        k = m_kee;
        f = m_fe;
        if (m_count == 0) {
            return;
        }
        const Coupling x = m_ldlt.solve(m_kei.transpose()).transpose();
        k.noalias() -= x * m_kei.transpose();
        f.noalias() -= x * m_fi;
    }

    // The flexible dofs, global components, from the external local ones.
    [[nodiscard]] Vec18 flexibleGlobal(const Vec18& external) const
    {
        Vec18 d = external;
        if (m_count > 0) {
            const InternalVector internal = m_ldlt.solve(m_fi - m_kei.transpose() * external);
            for (int i = 0; i < m_count; ++i) {
                d(m_external.at(static_cast<std::size_t>(i))) = internal(i);
            }
        }
        return m_local * d;
    }

private:
    Mat18 m_local = Mat18::Identity();
    int m_count = 0;
    std::array<int, kDofs> m_internal{-1, -1, -1, -1, -1, -1, -1, -1, -1,
                                      -1, -1, -1, -1, -1, -1, -1, -1, -1};
    std::array<int, kMaxInternal> m_external{};
    std::array<double, kMaxInternal> m_spring{};
    Mat18 m_kee = Mat18::Zero();
    Coupling m_kei;
    Internal m_kii;
    Vec18 m_fe = Vec18::Zero();
    InternalVector m_fi;
    Eigen::LDLT<Internal> m_ldlt;
};

// External local dofs from the global nodal dofs (6 per node): offsets and
// the axes of each end; the middle node as it is.
Mat18 nodesToExternal(const BeamData& beam, const Point& start, const Point& end)
{
    Mat18 a = Mat18::Identity();
    const std::array<const Point*, 2> ends = {&start, &end};
    for (int e = 0; e < 2; ++e) {
        const Eigen::Matrix3d r = ends.at(static_cast<std::size_t>(e))->axes.transpose();
        const Eigen::Index o = at(e);
        a.block<3, 3>(o, o) = r;
        a.block<3, 3>(o, o + 3) = -r * cross(beam.offsets.at(static_cast<std::size_t>(e)));
        a.block<3, 3>(o + 3, o + 3) = r;
    }
    return a;
}

} // namespace

IsoNodes isoNodes(const BeamData& beam, const ElementNodes& nodes)
{
    return {nodes.x[0] + beam.offsets[0], nodes.x[1] + beam.offsets[1], nodes.x[2]};
}

double isoLength(const IsoNodes& x)
{
    return arcLength(x, 1.0);
}

double isoParameter(const IsoNodes& x, double s)
{
    const double length = arcLength(x, 1.0);
    double xi = std::clamp(2.0 * s / length - 1.0, -1.0, 1.0);
    for (int iteration = 0; iteration < 50; ++iteration) {
        const double step = (arcLength(x, xi) - s) / jacobian(x, xi);
        xi = std::clamp(xi - step, -1.0, 1.0);
        if (std::abs(step) < 1e-15) {
            break;
        }
    }
    return xi;
}

IsoBeamElement::IsoBeamElement(ElementKind kind, std::span<const IsoBeamData> beams)
    : m_kind(kind)
    , m_beams(beams)
{}

DofMask IsoBeamElement::dofsAt(ElementType /*type*/, int /*localNode*/) const
{
    const DofMask plane = mask(DofType::U) | mask(DofType::V) | mask(DofType::RZ);
    return planarKind(m_kind) ? plane
                              : static_cast<DofMask>(plane | mask(DofType::W) | mask(DofType::RX) |
                                                     mask(DofType::RY));
}

Expected<void> IsoBeamElement::compute(const ElementNodes& nodes, ElementIndex element,
                                       Eigen::Ref<Eigen::MatrixXd> matrix,
                                       Eigen::Ref<Eigen::VectorXd> vector) const
{
    if (nodes.type != ElementType::Line3) {
        return makeError("An isoparametric curved beam must be a 3-node line.");
    }
    const IsoBeamData& data = m_beams[static_cast<std::size_t>(element)];
    const IsoNodes x = isoNodes(data.beam, nodes);
    const auto f = flexible(data, m_kind, x);
    const auto start = pointAt(x, data, m_kind, -1.0);
    const auto end = pointAt(x, data, m_kind, 1.0);
    if (!f || !start || !end) {
        return makeError("The curve of the element is not valid.");
    }
    const IsoConnections connections(data, m_kind, *start, *end, *f);
    Mat18 k;
    Vec18 v;
    connections.condense(k, v);
    const Mat18 a = nodesToExternal(data.beam, *start, *end);
    const Mat18 kg = a.transpose() * k * a;
    const Vec18 fg = a.transpose() * v;
    if (planarKind(m_kind)) {
        for (int i = 0; i < 9; ++i) {
            const int fi = kNodeDofs * (i / 3) + kPlanar.at(static_cast<std::size_t>(i % 3));
            vector(i) = fg(fi);
            for (int j = 0; j < 9; ++j) {
                const int fj = kNodeDofs * (j / 3) + kPlanar.at(static_cast<std::size_t>(j % 3));
                matrix(i, j) = kg(fi, fj);
            }
        }
    } else {
        matrix = kg;
        vector = fg;
    }
    return {};
}

BeamPoint isoBeamAt(const IsoBeamData& data, ElementKind kind, const ElementNodes& nodes,
                    std::span<const double> nodeValues, double s)
{
    const IsoNodes x = isoNodes(data.beam, nodes);
    BeamPoint out;
    const auto f = flexible(data, kind, x);
    const auto start = pointAt(x, data, kind, -1.0);
    const auto end = pointAt(x, data, kind, 1.0);
    const auto here = pointAt(x, data, kind, isoParameter(x, s));
    if (!f || !start || !end || !here) {
        return out;
    }
    Vec18 values = Vec18::Zero();
    if (planarKind(kind)) {
        for (int i = 0; i < 9; ++i) {
            values(kNodeDofs * (i / 3) + kPlanar.at(static_cast<std::size_t>(i % 3))) =
                nodeValues[static_cast<std::size_t>(i)];
        }
    } else {
        for (int i = 0; i < kDofs; ++i) {
            values(i) = nodeValues[static_cast<std::size_t>(i)];
        }
    }
    const IsoConnections connections(data, kind, *start, *end, *f);
    const Vec18 q = connections.flexibleGlobal(nodesToExternal(data.beam, *start, *end) * values);
    Eigen::Vector3d u = Eigen::Vector3d::Zero();
    Eigen::Vector3d theta = Eigen::Vector3d::Zero();
    for (int i = 0; i < 3; ++i) {
        const double n = here->shape.n.at(static_cast<std::size_t>(i));
        u += n * q.segment<3>(at(i));
        theta += n * q.segment<3>(at(i) + 3);
    }
    out.displacement = here->axes.transpose() * u;
    out.rotation = here->axes.transpose() * theta;
    const BeamData& beam = data.beam;
    const Mat6 d = constitutive(beam, arcLength(x, 1.0));
    Vec6 imposed;
    imposed << data.loads.imposedStrain, 0.0, 0.0, 0.0, data.loads.curvatureY,
        data.loads.curvatureZ;
    // Curvatures here; normal and shear strains from the reduced points,
    // extrapolated linearly in xi.
    Vec6 strain = strainAt(*here) * q - imposed;
    std::array<Vec6, 2> reduced{};
    for (std::size_t g = 0; g < 2; ++g) {
        const auto p = pointAt(x, data, kind, kGauss2.at(g));
        if (!p) {
            return out;
        }
        reduced.at(g) = strainAt(*p) * q - imposed;
    }
    const double xi = isoParameter(x, s);
    const double slope = (xi - kGauss2[0]) / (kGauss2[1] - kGauss2[0]);
    strain.head<3>() = (1.0 - slope) * reduced[0].head<3>() + slope * reduced[1].head<3>();
    const Vec6 forces = d * strain;
    BeamForces& bf = out.forces;
    bf.n = forces(0);
    bf.vy = forces(1);
    bf.vz = forces(2);
    bf.t = forces(3) + beam.shearCentre.y() * forces(1) - beam.shearCentre.x() * forces(2);
    bf.my = forces(4);
    bf.mz = forces(5);
    return out;
}

Expected<void> isoCheck(const IsoBeamData& data, ElementKind kind, const ElementNodes& nodes)
{
    const IsoNodes x = isoNodes(data.beam, nodes);
    for (const double xi : {-1.0, kGauss3[0], kGauss2[0], 0.0, kGauss2[1], kGauss3[2], 1.0}) {
        if (auto p = pointAt(x, data, kind, xi); !p) {
            return tl::unexpected(p.error());
        }
    }
    return {};
}

bool hasStableIsoConnections(const IsoBeamData& data, ElementKind kind, const ElementNodes& nodes)
{
    const IsoNodes x = isoNodes(data.beam, nodes);
    const auto f = flexible(data, kind, x);
    const auto start = pointAt(x, data, kind, -1.0);
    const auto end = pointAt(x, data, kind, 1.0);
    if (!f || !start || !end) {
        return false;
    }
    const IsoConnections connections(data, kind, *start, *end, *f);
    return connections.stable();
}

} // namespace fem::core
