#include <fem/core/beam_result.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace fem::core {

namespace {

// Gauss rules on [0, 1]: 3 points (degree 5) for the span loads, linear
// times (x - t)^3; 4 points (degree 7) for the foundation, cubic times
// (x - t)^3.
constexpr std::array<double, 3> kGauss3 = {0.1127016653792583, 0.5, 0.8872983346207417};
constexpr std::array<double, 3> kWeights3 = {5.0 / 18.0, 8.0 / 18.0, 5.0 / 18.0};
constexpr std::array<double, 4> kGauss4 = {0.0694318442029737, 0.3300094782075719,
                                           0.6699905217924281, 0.9305681557970263};
constexpr std::array<double, 4> kWeights4 = {0.1739274225687269, 0.3260725774312731,
                                             0.3260725774312731, 0.1739274225687269};
constexpr std::array<double, 4> kFactorial = {1.0, 1.0, 2.0, 6.0};
const Eigen::Vector4d kFlipXZ(1.0, -1.0, 1.0, -1.0);

// I_k(x) = integral over [0, x] of (x - t)^k / k! q(t) dt, plus the same
// for the point loads: q has (qx, qy, qz, mx), points (Px, Py, Pz, Mx, My, Mz).
struct Integrals
{
    Eigen::Matrix4d q = Eigen::Matrix4d::Zero(); // (component, k)
    Eigen::Matrix<double, 6, 3> p = Eigen::Matrix<double, 6, 3>::Zero();

    void addDistributed(double x, double t, double weight, const Eigen::Vector4d& value)
    {
        double power = 1.0;
        for (int k = 0; k < 4; ++k) {
            q.col(k) += weight * power / kFactorial[static_cast<std::size_t>(k)] * value;
            power *= x - t;
        }
    }
};

} // namespace

void endForces(const BeamData& beam, const BeamVector& forces, BeamForces& start, BeamForces& end)
{
    // The torque about the shear centre: T_S = T_C + ez Fv - ey Fw.
    const auto torque = [&beam, &forces](int o) {
        return forces(o + 3) + beam.shearCentre.y() * forces(o + 1) -
               beam.shearCentre.x() * forces(o + 2);
    };
    // The start node pulls on the face of normal -x: minus the internal force.
    start = {.n = -forces(0),
             .vy = -forces(1),
             .vz = -forces(2),
             .t = -torque(0),
             .my = -forces(4),
             .mz = -forces(5)};
    end = {.n = forces(6),
           .vy = forces(7),
           .vz = forces(8),
           .t = torque(6),
           .my = forces(10),
           .mz = forces(11)};
}

BeamResult::BeamResult(Data data)
    : m_data(std::move(data))
{}

BeamPoint BeamResult::at(double x, Side side) const
{
    const BeamData& beam = m_data.beam;
    const SpanLoads& loads = m_data.loads;
    const double length = m_data.length;
    const double ey = beam.shearCentre.x();
    const double ez = beam.shearCentre.y();

    Integrals in;
    for (const SpanPiece& piece : loads.pieces) {
        const double span = piece.to - piece.from;
        const double to = std::min(piece.to, x);
        const double clipped = to - piece.from;
        if (!(span > 0.0) || !(clipped > 0.0)) {
            continue;
        }
        for (std::size_t g = 0; g < kGauss3.size(); ++g) {
            const double t = piece.from + kGauss3[g] * clipped;
            const double s = (t - piece.from) / span;
            in.addDistributed(x, t, kWeights3[g] * clipped,
                              (1.0 - s) * piece.start + s * piece.end);
        }
    }
    for (const SpanPoint& point : loads.points) {
        if (point.at < x || (side == Side::After && point.at == x)) {
            Eigen::Matrix<double, 6, 1> value;
            value << point.force, point.moment;
            double power = 1.0;
            for (int k = 0; k < 3; ++k) {
                in.p.col(k) += power / kFactorial[static_cast<std::size_t>(k)] * value;
                power *= x - point.at;
            }
        }
    }
    // REQ-008-15: the reaction of the foundation on the interpolated
    // displacements, as a distributed load.
    const BeamVector& d = m_data.displacements;
    if (!beam.foundation.isZero() && x > 0.0) {
        const double phiY = shearParameter(beam, length, true);
        const double phiZ = shearParameter(beam, length, false);
        const Eigen::Vector4d dv(d(1), d(5), d(7), d(11));
        const Eigen::Vector4d dw(d(2), d(4), d(8), d(10));
        for (std::size_t g = 0; g < kGauss4.size(); ++g) {
            const double t = kGauss4[g] * x;
            const double xi = t / length;
            const double u = (1.0 - xi) * d(0) + xi * d(6);
            const double rx = (1.0 - xi) * d(3) + xi * d(9);
            const double v = planeShape(xi, length, phiY).displacement.dot(dv);
            const double w =
                kFlipXZ.cwiseProduct(planeShape(xi, length, phiZ).displacement).dot(dw);
            Eigen::Vector4d q(-beam.foundation(0) * u, -beam.foundation(1) * v,
                              -beam.foundation(2) * w, -beam.foundation(3) * rx);
            q(3) += ez * q(1) - ey * q(2); // lever about the shear centre
            in.addDistributed(x, t, kWeights4[g] * x, q);
        }
    }

    const BeamForces& s = m_data.start;
    BeamPoint r;
    BeamForces& f = r.forces;
    f.n = s.n - in.q(0, 0) - in.p(0, 0);
    f.vy = s.vy - in.q(1, 0) - in.p(1, 0);
    f.vz = s.vz - in.q(2, 0) - in.p(2, 0);
    f.t = s.t - in.q(3, 0) - in.p(3, 0);
    // dMz/dx = -Vy, dMy/dx = Vz.
    f.mz = s.mz - s.vy * x + in.q(1, 1) + in.p(1, 1) - in.p(5, 0);
    f.my = s.my + s.vz * x - in.q(2, 1) - in.p(2, 1) - in.p(4, 0);

    // rz' = Mz/EIz + kz, v' = rz + Vy/GAsy; ry' = My/EIy + ky, w' = -ry + Vz/GAsz.
    const double x2 = x * x / 2.0;
    const double x3 = x * x * x / 6.0;
    const double intMz = s.mz * x - s.vy * x2 + in.q(1, 2) + in.p(1, 2) - in.p(5, 1);
    const double intVy = s.vy * x - in.q(1, 1) - in.p(1, 1);
    const double intVz = s.vz * x - in.q(2, 1) - in.p(2, 1);
    const double intMy = s.my * x + s.vz * x2 - in.q(2, 2) - in.p(2, 2) - in.p(4, 1);

    // The second integrals of the moments need (x - a)^3 / 6 of the point
    // forces, one power beyond Integrals::p.
    double pointForceY3 = 0.0;
    double pointForceZ3 = 0.0;
    for (const SpanPoint& point : loads.points) {
        if (point.at < x || (side == Side::After && point.at == x)) {
            const double a = x - point.at;
            pointForceY3 += point.force.y() * a * a * a / 6.0;
            pointForceZ3 += point.force.z() * a * a * a / 6.0;
        }
    }
    const double int2Mz = s.mz * x2 - s.vy * x3 + in.q(1, 3) + pointForceY3 - in.p(5, 2);
    const double int2My = s.my * x2 + s.vz * x3 - in.q(2, 3) - pointForceZ3 - in.p(4, 2);

    const Eigen::Vector3d u0(d(0), d(1) - ez * d(3), d(2) + ey * d(3)); // shear centre
    const Eigen::Vector3d r0(d(3), d(4), d(5));
    const double rz = r0.z() + intMz / beam.bendingZ + loads.curvatureZ * x;
    double v = u0.y() + r0.z() * x + int2Mz / beam.bendingZ + loads.curvatureZ * x2;
    if (beam.shearY > 0.0) {
        v += intVy / beam.shearY;
    }
    double ry = r0.y();
    double w = u0.z() - r0.y() * x;
    if (beam.bendingY > 0.0) {
        ry += intMy / beam.bendingY + loads.curvatureY * x;
        w -= int2My / beam.bendingY + loads.curvatureY * x2;
    }
    if (beam.shearZ > 0.0) {
        w += intVz / beam.shearZ;
    }
    double rx = r0.x();
    if (beam.torsional > 0.0) {
        rx += (s.t * x - in.q(3, 1) - in.p(3, 1)) / beam.torsional;
    }
    const double u =
        u0.x() + (s.n * x - in.q(0, 1) - in.p(0, 1)) / beam.axial + loads.imposedStrain * x;
    // Back to the centroid: v_C = v_S + ez rx, w_C = w_S - ey rx.
    r.displacement = Eigen::Vector3d(u, v + ez * rx, w - ey * rx);
    r.rotation = Eigen::Vector3d(rx, ry, rz);
    return r;
}

namespace {

double normalAt(const SectionProperties& section, const BeamForces& forces,
                const Eigen::Vector2d& principal)
{
    return forces.n / section.area - forces.mz * principal.x() / section.inertiaZ +
           forces.my * principal.y() / section.inertiaY;
}

} // namespace

double normalStress(const SectionProperties& section, const BeamForces& forces,
                    const Eigen::Vector2d& point)
{
    const double c = std::cos(section.principalAngle);
    const double s = std::sin(section.principalAngle);
    return normalAt(section, forces,
                    Eigen::Vector2d(c * point.x() + s * point.y(), -s * point.x() + c * point.y()));
}

std::vector<PointStress> sectionStresses(const Section& section, const BeamForces& forces)
{
    if (!section.shape) {
        return {};
    }
    const auto properties = sectionProperties(section);
    if (!properties) {
        return {};
    }
    std::vector<PointStress> stresses;
    for (const StressPoint& point : stressPoints(*section.shape)) {
        PointStress stress;
        stress.name = point.name;
        stress.normal = normalAt(*properties, forces, point.position);
        if (point.thicknessY > 0.0) {
            stress.shear += std::abs(forces.vy) * point.firstMomentY /
                            (properties->inertiaZ * point.thicknessY);
        }
        if (point.thicknessZ > 0.0) {
            stress.shear += std::abs(forces.vz) * point.firstMomentZ /
                            (properties->inertiaY * point.thicknessZ);
        }
        stress.shear += std::abs(forces.t) * point.torsionFactor;
        stress.vonMises =
            std::sqrt(stress.normal * stress.normal + 3.0 * stress.shear * stress.shear);
        stresses.push_back(std::move(stress));
    }
    return stresses;
}

} // namespace fem::core
