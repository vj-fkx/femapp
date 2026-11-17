// REQ-008b-07, 09, 11, 13: the exact beam element (design 008b §5.3, 5.4):
// the flexible length of the state engine, its connections and the
// transformation to the nodes, with a warping dof per end.

#include <fem/core/beam_state.hpp>

#include <Eigen/Cholesky>
#include <array>
#include <cstddef>

namespace fem::core {

namespace {

constexpr double kSingular = 1e-12; // pivot of the internal dofs, of the largest

using SystemMatrix =
    Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, 0, kMaxStateDofs, kMaxStateDofs>;
using SystemVector = Eigen::Matrix<double, Eigen::Dynamic, 1, 0, kMaxStateDofs, 1>;

bool planarKind(ElementKind kind)
{
    return kind == ElementKind::Beam2D;
}

// The components of the 008 springs for the local dofs of one end.
constexpr std::array<int, 3> kPlanarComponents = {0, 1, 5};

// What each local dof of the flexible length is tied to (design 008 §5.4
// and 008b §5.4): an external dof (rigid), an internal one (a spring to
// the external, or none for a free warping), or nothing (a restrained
// warping, held at zero).
struct Layout
{
    int dofs = 0;     // per end of the flexible length
    int external = 0; // external local dofs, both ends
    int internal = 0;
    std::array<int, kMaxStateDofs> externalOf{}; // per flexible dof, -1 if none
    std::array<int, kMaxStateDofs> internalOf{}; // per flexible dof, -1 if none
    std::array<double, kMaxStateDofs> spring{};  // per flexible dof with both
    std::array<int, 2> firstExternal{};          // per end
    std::array<int, 2> endExternal{};            // external count per end
};

Layout layoutOf(const BeamData& beam, ElementKind kind)
{
    Layout l;
    const bool planar = planarKind(kind);
    l.dofs = stateDofs(beam, planar);
    const int translational = planar ? 3 : 6;
    for (int end = 0; end < 2; ++end) {
        l.firstExternal.at(static_cast<std::size_t>(end)) = l.external;
        for (int c = 0; c < l.dofs; ++c) {
            const auto j = static_cast<std::size_t>(end) * static_cast<std::size_t>(l.dofs) +
                           static_cast<std::size_t>(c);
            l.externalOf.at(j) = -1;
            l.internalOf.at(j) = -1;
            if (c >= translational) { // the warping
                const WarpingCondition condition =
                    beam.warpingEnds.at(static_cast<std::size_t>(end));
                if (condition == WarpingCondition::Free) {
                    l.internalOf.at(j) = l.internal++;
                }
                continue;
            }
            const int component = planar ? kPlanarComponents.at(static_cast<std::size_t>(c)) : c;
            const double spring = beam.springs.at(static_cast<std::size_t>(end))
                                      .at(static_cast<std::size_t>(component));
            l.externalOf.at(j) = l.external++;
            if (spring != kRigid) {
                l.internalOf.at(j) = l.internal++;
                l.spring.at(j) = spring;
            }
        }
        l.endExternal.at(static_cast<std::size_t>(end)) =
            l.external - l.firstExternal.at(static_cast<std::size_t>(end));
    }
    return l;
}

// The flexible length and its connections, condensed on the external dofs.
class ExactConnections
{
public:
    ExactConnections(const Layout& layout, const StateBarMatrix& k, const StateBarVector& f)
        : m_layout(layout)
    {
        const int ne = layout.external;
        const int ni = layout.internal;
        m_kee = SystemMatrix::Zero(ne, ne);
        m_kei = SystemMatrix::Zero(ne, ni);
        m_kii = SystemMatrix::Zero(ni, ni);
        m_fe = SystemVector::Zero(ne);
        m_fi = SystemVector::Zero(ni);
        const int n = 2 * layout.dofs;
        for (int a = 0; a < n; ++a) {
            const auto sa = static_cast<std::size_t>(a);
            const int ia = layout.internalOf.at(sa);
            const int ea = ia < 0 ? layout.externalOf.at(sa) : -1;
            if (ia < 0 && ea < 0) {
                continue; // held at zero
            }
            for (int b = 0; b < n; ++b) {
                const auto sb = static_cast<std::size_t>(b);
                const int ib = layout.internalOf.at(sb);
                const int eb = ib < 0 ? layout.externalOf.at(sb) : -1;
                if (ib < 0 && eb < 0) {
                    continue;
                }
                const double value = k(a, b);
                if (ia < 0 && ib < 0) {
                    m_kee(ea, eb) += value;
                } else if (ia < 0) {
                    m_kei(ea, ib) += value;
                } else if (ib >= 0) {
                    m_kii(ia, ib) += value;
                }
            }
            (ia < 0 ? m_fe(ea) : m_fi(ia)) += f(a);
        }
        // The springs between the external dof and its internal one.
        for (int a = 0; a < n; ++a) {
            const auto sa = static_cast<std::size_t>(a);
            const int i = layout.internalOf.at(sa);
            const int e = layout.externalOf.at(sa);
            if (i >= 0 && e >= 0) {
                const double s = layout.spring.at(sa);
                m_kee(e, e) += s;
                m_kii(i, i) += s;
                m_kei(e, i) -= s;
            }
        }
        if (ni > 0) {
            m_ldlt.compute(m_kii);
        }
    }

    [[nodiscard]] bool stable() const
    {
        if (m_layout.internal == 0) {
            return true;
        }
        const auto d = m_ldlt.vectorD();
        const double largest =
            std::max(d.cwiseAbs().maxCoeff(), m_kii.diagonal().cwiseAbs().maxCoeff());
        return m_ldlt.info() == Eigen::Success && d.minCoeff() > kSingular * largest;
    }

    void condense(SystemMatrix& k, SystemVector& f) const
    {
        k = m_kee;
        f = m_fe;
        if (m_layout.internal == 0) {
            return;
        }
        const SystemMatrix x = m_ldlt.solve(m_kei.transpose()).transpose();
        k.noalias() -= x * m_kei.transpose();
        f.noalias() -= x * m_fi;
    }

    // The flexible dofs from the external ones.
    [[nodiscard]] StateBarVector flexible(const SystemVector& external) const
    {
        const int n = 2 * m_layout.dofs;
        SystemVector internal = SystemVector::Zero(m_layout.internal);
        if (m_layout.internal > 0) {
            internal = m_ldlt.solve(m_fi - m_kei.transpose() * external);
        }
        StateBarVector d = StateBarVector::Zero(n);
        for (int a = 0; a < n; ++a) {
            const auto sa = static_cast<std::size_t>(a);
            if (m_layout.internalOf.at(sa) >= 0) {
                d(a) = internal(m_layout.internalOf.at(sa));
            } else if (m_layout.externalOf.at(sa) >= 0) {
                d(a) = external(m_layout.externalOf.at(sa));
            }
        }
        return d;
    }

private:
    const Layout& m_layout;
    SystemMatrix m_kee;
    SystemMatrix m_kei;
    SystemMatrix m_kii;
    SystemVector m_fe;
    SystemVector m_fi;
    Eigen::LDLT<SystemMatrix> m_ldlt;
};

Eigen::Matrix3d cross(const Eigen::Vector3d& r)
{
    Eigen::Matrix3d m;
    m << 0.0, -r.z(), r.y(), r.z(), 0.0, -r.x(), -r.y(), r.x(), 0.0;
    return m;
}

// External local dofs from the element's nodal dofs (per node u, v, w, rx,
// ry, rz [, warp], or u, v, rz in the plane): the offsets and the rotation
// to the principal axes of each end; the warping passes as it is.
SystemMatrix nodesToExternal(const BeamData& beam, ElementKind kind, const Layout& layout)
{
    const bool planar = planarKind(kind);
    const int perNodeBase = planar ? 3 : 6;
    int columns = 0;
    std::array<int, 2> firstColumn{};
    for (std::size_t end = 0; end < 2; ++end) {
        firstColumn.at(end) = columns;
        columns += layout.endExternal.at(end);
    }
    SystemMatrix t = SystemMatrix::Zero(layout.external, columns);
    for (std::size_t end = 0; end < 2; ++end) {
        const Eigen::Matrix3d r = (end == 0 ? beam.axes : beam.endAxes).transpose();
        Eigen::Matrix<double, 6, 6> full = Eigen::Matrix<double, 6, 6>::Zero();
        full.topLeftCorner<3, 3>() = r;
        full.topRightCorner<3, 3>() = -r * cross(beam.offsets.at(end));
        full.bottomRightCorner<3, 3>() = r;
        const int row0 = layout.firstExternal.at(end);
        const int col0 = firstColumn.at(end);
        for (int i = 0; i < perNodeBase; ++i) {
            const int fi = planar ? kPlanarComponents.at(static_cast<std::size_t>(i)) : i;
            for (int j = 0; j < perNodeBase; ++j) {
                const int fj = planar ? kPlanarComponents.at(static_cast<std::size_t>(j)) : j;
                t(row0 + i, col0 + j) = full(fi, fj);
            }
        }
        if (layout.endExternal.at(end) > perNodeBase) {
            t(row0 + perNodeBase, col0 + perNodeBase) = 1.0; // the warping
        }
    }
    return t;
}

} // namespace

double exactLength(const BeamData& beam, const Eigen::Vector3d& x1, const Eigen::Vector3d& x2)
{
    return beam.arcLength > 0.0 ? beam.arcLength : flexibleLength(beam, x1, x2);
}

ExactBeamElement::ExactBeamElement(ElementKind kind, std::span<const BeamData> beams,
                                   std::span<const SpanLoads> loads)
    : m_kind(kind)
    , m_beams(beams)
    , m_loads(loads)
{}

DofMask ExactBeamElement::dofsAt(ElementType type, int localNode) const
{
    if (type == ElementType::Line3 && localNode == 2) {
        return 0; // the middle node of an arc only shapes it
    }
    const DofMask plane = mask(DofType::U) | mask(DofType::V) | mask(DofType::RZ);
    if (planarKind(m_kind)) {
        return plane;
    }
    return static_cast<DofMask>(plane | mask(DofType::W) | mask(DofType::RX) | mask(DofType::RY));
}

Expected<void> ExactBeamElement::compute(const ElementNodes& nodes, ElementIndex element,
                                         Eigen::Ref<Eigen::MatrixXd> matrix,
                                         Eigen::Ref<Eigen::VectorXd> vector) const
{
    if (nodes.type != ElementType::Line2 && nodes.type != ElementType::Line3) {
        return makeError("An exact beam element must be a 2- or 3-node line.");
    }
    const auto index = static_cast<std::size_t>(element);
    const BeamData& beam = m_beams[index];
    const double length = exactLength(beam, nodes.x[0], nodes.x[1]);
    if (!(length > 0.0)) {
        return makeError("The bar has zero length.");
    }
    const StateBarMatrices bar = stateBarMatrices(beam, length, m_loads[index], planarKind(m_kind));
    const Layout layout = layoutOf(beam, m_kind);
    const ExactConnections connections(layout, bar.stiffness, bar.loads);
    SystemMatrix k;
    SystemVector f;
    connections.condense(k, f);
    const SystemMatrix t = nodesToExternal(beam, m_kind, layout);
    matrix = t.transpose() * k * t;
    vector = t.transpose() * f;
    return {};
}

StateBarVector exactBeamEnds(const BeamData& beam, ElementKind kind, const SpanLoads& loads,
                             const Eigen::Vector3d& x1, const Eigen::Vector3d& x2,
                             std::span<const double> nodeValues, StateBarVector* endLoads)
{
    const double length = exactLength(beam, x1, x2);
    const StateBarMatrices bar = stateBarMatrices(beam, length, loads, planarKind(kind));
    const Layout layout = layoutOf(beam, kind);
    const ExactConnections connections(layout, bar.stiffness, bar.loads);
    const SystemMatrix t = nodesToExternal(beam, kind, layout);
    SystemVector values(t.cols());
    for (Eigen::Index i = 0; i < t.cols(); ++i) {
        values(i) = nodeValues[static_cast<std::size_t>(i)];
    }
    StateBarVector ends = connections.flexible(t * values);
    if (endLoads != nullptr) {
        *endLoads = bar.stiffness * ends - bar.loads;
    }
    return ends;
}

StateBarVector exactEndLoads(const BeamData& beam, double length, const SpanLoads& loads,
                             bool planar, const StateBarVector& ends)
{
    const StateBarMatrices bar = stateBarMatrices(beam, length, loads, planar);
    return bar.stiffness * ends - bar.loads;
}

bool hasStableExactConnections(const BeamData& beam, ElementKind kind, double length)
{
    const StateBarMatrices bar = stateBarMatrices(beam, length, SpanLoads{}, planarKind(kind));
    const Layout layout = layoutOf(beam, kind);
    const ExactConnections connections(layout, bar.stiffness, bar.loads);
    return connections.stable();
}

} // namespace fem::core
