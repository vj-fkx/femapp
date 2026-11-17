#pragma once

// An independent reference for circular arcs in the XY plane (centre at the
// origin), by the principle of virtual work: the internal forces of a
// statically determinate structure are exact, and the integrals of their
// products with the compliances of the section are done by Gauss-Legendre
// quadrature of high order (the integrands are smooth trigonometric
// functions). With the force method, the same gives indeterminate arcs.

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <array>
#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

namespace fem::core::test {

// The six internal forces (N, Vy, Vz, T, My, Mz) in the local axes of a
// section, and the compliances that go with them.
using Internal = Eigen::Matrix<double, 6, 1>;

struct ArcSection
{
    double ea = 0.0;
    double gay = 0.0; // G Asy
    double gaz = 0.0;
    double gj = 0.0;
    double eiy = 0.0;
    double eiz = 0.0;

    [[nodiscard]] Internal compliance() const
    {
        Internal c;
        c << 1.0 / ea, 1.0 / gay, 1.0 / gaz, 1.0 / gj, 1.0 / eiy, 1.0 / eiz;
        return c;
    }
};

// Gauss-Legendre points and weights on [-1, 1] (Newton on P_n).
inline std::vector<std::array<double, 2>> gaussLegendre(int n)
{
    std::vector<std::array<double, 2>> rule;
    for (int i = 1; i <= n; ++i) {
        double x = std::cos(std::numbers::pi * (i - 0.25) / (n + 0.5));
        double derivative = 0.0;
        for (int iteration = 0; iteration < 100; ++iteration) {
            double p0 = 1.0;
            double p1 = x;
            for (int k = 2; k <= n; ++k) {
                const double p2 = ((2.0 * k - 1.0) * x * p1 - (k - 1.0) * p0) / k;
                p0 = p1;
                p1 = p2;
            }
            derivative = n * (x * p1 - p0) / (x * x - 1.0);
            const double step = p1 / derivative;
            x -= step;
            if (std::abs(step) < 1e-16) {
                break;
            }
        }
        rule.push_back({x, 2.0 / ((1.0 - x * x) * derivative * derivative)});
    }
    return rule;
}

// A circular arc of radius r in the XY plane, at angles theta (from +x,
// counterclockwise). `planar` picks the local axes of a 2D beam (y in the
// plane, z = Z); otherwise those of a 3D beam with the default reference
// (y = Z, z = x × y).
struct ArcReference
{
    double r = 1.0;
    ArcSection section;
    bool planar = false;

    [[nodiscard]] Eigen::Vector3d point(double theta) const
    {
        return {r * std::cos(theta), r * std::sin(theta), 0.0};
    }

    [[nodiscard]] Eigen::Matrix3d axes(double theta) const
    {
        const Eigen::Vector3d t(-std::sin(theta), std::cos(theta), 0.0);
        const Eigen::Vector3d z = Eigen::Vector3d::UnitZ();
        Eigen::Matrix3d a;
        if (planar) {
            a << t, z.cross(t), z;
        } else {
            a << t, z, t.cross(z);
        }
        return a;
    }

    // The internal forces at theta from a force and a moment (about the
    // section) that the part ahead applies on it.
    [[nodiscard]] Internal local(double theta, const Eigen::Vector3d& force,
                                 const Eigen::Vector3d& moment) const
    {
        const Eigen::Matrix3d a = axes(theta);
        Internal x;
        x << a.transpose() * force, a.transpose() * moment;
        return x;
    }

    // From a force and a couple at the angle `at` (on the part ahead, beyond
    // theta toward increasing angles when `at` > theta).
    [[nodiscard]] Internal fromPoint(double theta, double at, const Eigen::Vector3d& force,
                                     const Eigen::Vector3d& couple) const
    {
        return local(theta, force, (point(at) - point(theta)).cross(force) + couple);
    }

    // From a uniform force per length of arc, fixed in space, on (theta, to].
    [[nodiscard]] Internal fromUniform(double theta, double to, const Eigen::Vector3d& q) const
    {
        // integral of p(t) dt over [theta, to]
        const Eigen::Vector3d integral(r * (std::sin(to) - std::sin(theta)),
                                       r * (std::cos(theta) - std::cos(to)), 0.0);
        const Eigen::Vector3d force = q * r * (to - theta);
        const Eigen::Vector3d moment = r * (integral - (to - theta) * point(theta)).cross(q);
        return local(theta, force, moment);
    }

    // From forces q and couples c per length of arc (global, functions of
    // the angle) on [from, to], at the section theta, by Gauss-Legendre.
    [[nodiscard]] Internal fromLoad(double theta, double from, double to,
                                    const std::function<Eigen::Vector3d(double)>& q,
                                    const std::function<Eigen::Vector3d(double)>& c) const
    {
        static const auto rule = gaussLegendre(64);
        Eigen::Vector3d force = Eigen::Vector3d::Zero();
        Eigen::Vector3d moment = Eigen::Vector3d::Zero();
        if (to > from) {
            for (const auto& [x, w] : rule) {
                const double at = (from + to) / 2.0 + (to - from) / 2.0 * x;
                const double ds = w * r * (to - from) / 2.0;
                force += ds * q(at);
                moment += ds * ((point(at) - point(theta)).cross(q(at)) + c(at));
            }
        }
        return local(theta, force, moment);
    }

    // The integral over [from, to] of a . c . b ds.
    [[nodiscard]] double work(double from, double to, const std::function<Internal(double)>& a,
                              const std::function<Internal(double)>& b) const
    {
        static const auto rule = gaussLegendre(64);
        const Internal c = section.compliance();
        double sum = 0.0;
        for (const auto& [x, w] : rule) {
            const double theta = (from + to) / 2.0 + (to - from) / 2.0 * x;
            sum += w * a(theta).cwiseProduct(c).dot(b(theta));
        }
        return sum * r * (to - from) / 2.0;
    }
};

} // namespace fem::core::test
