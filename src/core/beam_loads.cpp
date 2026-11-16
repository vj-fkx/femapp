#include <fem/core/beam_loads.hpp>

#include <array>
#include <cmath>
#include <cstddef>

namespace fem::core {

namespace {

// 3-point Gauss rule on [0, 1]: exact to degree 5, enough for the cubic
// functions of the theory times a linear load.
constexpr std::array<double, 3> kGaussPoints = {0.1127016653792583, 0.5, 0.8872983346207417};
constexpr std::array<double, 3> kGaussWeights = {5.0 / 18.0, 8.0 / 18.0, 5.0 / 18.0};

constexpr std::array<int, 4> kPlaneXY = {1, 5, 7, 11};
constexpr std::array<int, 4> kPlaneXZ = {2, 4, 8, 10};
const Eigen::Vector4d kFlipXZ(1.0, -1.0, 1.0, -1.0);

// From the local axes of the user to the principal ones (a turn about x).
Eigen::Vector3d toPrincipal(const SpanFrame& frame, Axes axes, const Eigen::Vector3d& v)
{
    if (axes == Axes::Global) {
        return frame.principalAxes.transpose() * v;
    }
    const double c = std::cos(frame.principalAngle);
    const double s = std::sin(frame.principalAngle);
    return {v.x(), c * v.y() + s * v.z(), -s * v.y() + c * v.z()};
}

// The torque about the shear centre of a force on the centroid:
// (C - S) x F, with C - S = -(ey, ez).
double leverTorque(const SpanFrame& frame, const Eigen::Vector3d& force)
{
    return frame.shearCentre.y() * force.y() - frame.shearCentre.x() * force.z();
}

// Adds to `f` (on the shear centre) a force and a moment at xi.
void addPoint(BeamVector& f, double xi, double length, double phiY, double phiZ,
              const Eigen::Vector3d& force, const Eigen::Vector3d& moment)
{
    f(0) += (1.0 - xi) * force.x();
    f(6) += xi * force.x();
    f(3) += (1.0 - xi) * moment.x();
    f(9) += xi * moment.x();
    const PlaneShape sy = planeShape(xi, length, phiY);
    const PlaneShape sz = planeShape(xi, length, phiZ);
    for (std::size_t a = 0; a < 4; ++a) {
        const int i = static_cast<int>(a);
        // x-y: v and rz = dv/dx.
        f(kPlaneXY[a]) += sy.displacement(i) * force.y() + sy.rotation(i) * moment.z();
        // x-z: w on (w, -ry); ry = -dw/dx turns the sign of the rotation.
        f(kPlaneXZ[a]) +=
            kFlipXZ(i) * (sz.displacement(i) * force.z() - sz.rotation(i) * moment.y());
    }
}

} // namespace

double positionOn(const BarPosition& position, double length)
{
    return position.fraction ? position.value * length : position.value;
}

void addDistributed(SpanLoads& loads, const SpanFrame& frame, Axes axes,
                    const Eigen::Vector3d& startValue, const Eigen::Vector3d& endValue, double from,
                    double to, double startTorque, double endTorque)
{
    const Eigen::Vector3d q0 = toPrincipal(frame, axes, startValue);
    const Eigen::Vector3d q1 = toPrincipal(frame, axes, endValue);
    SpanPiece piece;
    piece.from = from;
    piece.to = to;
    piece.start << q0, leverTorque(frame, q0) + startTorque;
    piece.end << q1, leverTorque(frame, q1) + endTorque;
    loads.pieces.push_back(piece);
}

void addConcentrated(SpanLoads& loads, const SpanFrame& frame, Axes axes,
                     const Eigen::Vector3d& force, const Eigen::Vector3d& moment, double at,
                     double bimoment)
{
    SpanPoint point;
    point.at = at;
    point.force = toPrincipal(frame, axes, force);
    point.moment = toPrincipal(frame, axes, moment);
    point.moment.x() += leverTorque(frame, point.force);
    point.bimoment = bimoment;
    loads.points.push_back(point);
}

void addGradient(SpanLoads& loads, const SpanFrame& frame, double alpha, double alongY,
                 double alongZ)
{
    // The thermal strain alpha (gy y + gz z) in the principal axes; with
    // eps = u' - y rz' + z ry', it is the curvature rz' = -alpha gy',
    // ry' = alpha gz'.
    const Eigen::Vector3d g = toPrincipal(frame, Axes::Local, Eigen::Vector3d(0.0, alongY, alongZ));
    loads.curvatureZ += -alpha * g.y();
    loads.curvatureY += alpha * g.z();
}

BeamVector equivalentLoads(const BeamData& beam, double length, const SpanLoads& loads)
{
    const double phiY = shearParameter(beam, length, true);
    const double phiZ = shearParameter(beam, length, false);
    BeamVector f = BeamVector::Zero(); // on the shear centre
    for (const SpanPiece& piece : loads.pieces) {
        const double span = piece.to - piece.from;
        if (!(span > 0.0)) {
            continue;
        }
        for (std::size_t g = 0; g < kGaussPoints.size(); ++g) {
            const double t = kGaussPoints[g];
            const Eigen::Vector4d q = (1.0 - t) * piece.start + t * piece.end;
            const double x = piece.from + t * span;
            const double w = kGaussWeights[g] * span;
            addPoint(f, x / length, length, phiY, phiZ, w * q.head<3>(),
                     Eigen::Vector3d(w * q(3), 0.0, 0.0));
        }
    }
    for (const SpanPoint& point : loads.points) {
        addPoint(f, point.at / length, length, phiY, phiZ, point.force, point.moment);
    }
    // Imposed strain and curvatures: the end forces of the bar held fixed.
    const double n = beam.axial * loads.imposedStrain;
    f(0) -= n;
    f(6) += n;
    const double mz = beam.bendingZ * loads.curvatureZ;
    f(5) -= mz;
    f(11) += mz;
    const double my = beam.bendingY * loads.curvatureY;
    f(4) -= my;
    f(10) += my;

    // To the centroid: f_C = T_sc^T f_S (design 008 §5.4).
    for (int end = 0; end < 2; ++end) {
        const int o = end * kBeamEndDofs;
        f(o + 3) += -beam.shearCentre.y() * f(o + 1) + beam.shearCentre.x() * f(o + 2);
    }
    return f;
}

} // namespace fem::core
