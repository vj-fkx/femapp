// REQ-008b-03, 04: thin-walled sections described by their mid-line
// (design 008b §5.1.2). Classic thin-walled theory: Vlasov (1961), Murray
// (Introduction to the Theory of Thin-Walled Structures, 1984), Pilkey
// (Analysis and Design of Elastic Beams, 2002, chapter 6).

#include "detail/section_detail.hpp"

#include <Eigen/Cholesky>
#include <Eigen/Core>
#include <Eigen/LU>
#include <Eigen/QR>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fem::core::detail {

namespace {

using Vec = Eigen::Vector2d;

double cross(const Vec& a, const Vec& b)
{
    return a.x() * b.y() - a.y() * b.x();
}

// 3-point Gauss on [0, 1]: exact up to degree 5.
constexpr std::array<double, 3> kGaussPoints = {0.1127016653792583, 0.5, 0.8872983346207417};
constexpr std::array<double, 3> kGaussWeights = {5.0 / 18.0, 8.0 / 18.0, 5.0 / 18.0};

// Integral over a straight wall of the product of two linear functions
// (values a0, a1 and b0, b1 at the ends), per unit length.
double linearProduct(double a0, double a1, double b0, double b1)
{
    return (2.0 * a0 * b0 + a0 * b1 + a1 * b0 + 2.0 * a1 * b1) / 6.0;
}

// One step of a cell: a wall and whether the cell runs from its `from` to
// its `to` point.
struct CellStep
{
    std::size_t wall = 0;
    double sign = 1.0;
};

class ThinWalledSolver
{
public:
    ThinWalledSolver(const ThinWalled& shape, std::string_view section)
        : m_shape(shape)
        , m_prefix("Section \"" + std::string(section) + "\": ")
    {}

    Expected<ThinWalledData> solve()
    {
        if (auto problem = check()) {
            return makeError(*problem);
        }
        geometry();
        tree();
        cells();
        torsion();
        sectorial();
        ThinWalledData out;
        out.geometry = m_geometry;
        out.torsion = m_torsion;
        out.shearCentre = m_geometry.centroid + m_shearCentre;
        out.warping = m_warping;
        out.sectorial = m_omega;
        const double theta = principalAngle(m_geometry.iy, m_geometry.iz, m_geometry.iyz);
        out.shearAreaY = shearArea(theta, true, out.shearFlowY);
        out.shearAreaZ = shearArea(theta, false, out.shearFlowZ);
        out.torsionFlow = m_wallTorsionFlow;
        out.inCell = m_inCell;
        return out;
    }

private:
    [[nodiscard]] std::size_t pointCount() const { return m_shape.points.size(); }
    [[nodiscard]] std::size_t wallCount() const { return m_shape.walls.size(); }

    [[nodiscard]] std::size_t from(std::size_t w) const
    {
        return static_cast<std::size_t>(m_shape.walls[w].from);
    }
    [[nodiscard]] std::size_t to(std::size_t w) const
    {
        return static_cast<std::size_t>(m_shape.walls[w].to);
    }
    [[nodiscard]] double thickness(std::size_t w) const { return m_shape.walls[w].t; }
    [[nodiscard]] double length(std::size_t w) const
    {
        return (m_shape.points[to(w)] - m_shape.points[from(w)]).norm();
    }
    // A point relative to the centroid.
    [[nodiscard]] Vec local(std::size_t p) const { return m_shape.points[p] - m_geometry.centroid; }

    // REQ-008b-04: points, walls and connectivity (walls and points are
    // numbered from 0, as in the vectors).
    [[nodiscard]] std::optional<std::string> check()
    {
        const auto& points = m_shape.points;
        const auto& walls = m_shape.walls;
        if (walls.empty()) {
            return m_prefix + "a thin-walled section needs at least one wall.";
        }
        double extent = 0.0;
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (!points[i].allFinite()) {
                return m_prefix + "the point " + std::to_string(i) + " is not finite.";
            }
            for (std::size_t j = 0; j < i; ++j) {
                extent = std::max(extent, (points[i] - points[j]).norm());
            }
        }
        const double tolerance = 1e-12 * extent;
        for (std::size_t i = 0; i < points.size(); ++i) {
            for (std::size_t j = 0; j < i; ++j) {
                if ((points[i] - points[j]).norm() <= tolerance) {
                    return m_prefix + "the points " + std::to_string(j) + " and " +
                           std::to_string(i) + " coincide.";
                }
            }
        }
        const auto count = static_cast<int>(points.size());
        for (std::size_t w = 0; w < walls.size(); ++w) {
            const std::string name = "the wall " + std::to_string(w);
            const ThinWalled::Wall& wall = walls[w];
            if (wall.from < 0 || wall.from >= count || wall.to < 0 || wall.to >= count) {
                return m_prefix + name + " refers to a missing point.";
            }
            if (wall.from == wall.to) {
                return m_prefix + name + " has zero length.";
            }
            if (!(wall.t > 0.0) || !std::isfinite(wall.t)) {
                return m_prefix + "the thickness of " + name + " must be positive.";
            }
        }
        // Connected: every wall reached from the first.
        m_adjacent.assign(points.size(), {});
        for (std::size_t w = 0; w < walls.size(); ++w) {
            m_adjacent[from(w)].push_back(w);
            m_adjacent[to(w)].push_back(w);
        }
        std::vector<bool> reached(points.size(), false);
        std::vector<std::size_t> stack = {from(0)};
        reached[from(0)] = true;
        while (!stack.empty()) {
            const std::size_t p = stack.back();
            stack.pop_back();
            for (const std::size_t w : m_adjacent[p]) {
                const std::size_t q = from(w) == p ? to(w) : from(w);
                if (!reached[q]) {
                    reached[q] = true;
                    stack.push_back(q);
                }
            }
        }
        for (std::size_t w = 0; w < walls.size(); ++w) {
            if (!reached[from(w)]) {
                return m_prefix + "the walls are not connected (the wall " + std::to_string(w) +
                       " is apart).";
            }
        }
        return std::nullopt;
    }

    void geometry()
    {
        double area = 0.0;
        double sy = 0.0;
        double sz = 0.0;
        double yy = 0.0;
        double zz = 0.0;
        double yz = 0.0;
        for (std::size_t w = 0; w < wallCount(); ++w) {
            const Vec& a = m_shape.points[from(w)];
            const Vec& b = m_shape.points[to(w)];
            const double dA = length(w) * thickness(w);
            area += dA;
            sy += dA * (a.x() + b.x()) / 2.0;
            sz += dA * (a.y() + b.y()) / 2.0;
            yy += dA * linearProduct(a.x(), b.x(), a.x(), b.x());
            zz += dA * linearProduct(a.y(), b.y(), a.y(), b.y());
            yz += dA * linearProduct(a.x(), b.x(), a.y(), b.y());
        }
        m_geometry.area = area;
        m_geometry.centroid = Vec(sy / area, sz / area);
        const Vec& c = m_geometry.centroid;
        m_geometry.iz = yy - area * c.x() * c.x();
        m_geometry.iy = zz - area * c.y() * c.y();
        m_geometry.iyz = yz - area * c.x() * c.y();
    }

    // A spanning tree by breadth-first search from the first point of the
    // first wall; the other walls close the cells.
    void tree()
    {
        const std::size_t none = std::numeric_limits<std::size_t>::max();
        m_parentWall.assign(pointCount(), none);
        m_depth.assign(pointCount(), 0);
        m_inTree.assign(wallCount(), false);
        std::vector<bool> reached(pointCount(), false);
        const std::size_t root = from(0);
        reached[root] = true;
        m_order = {root};
        for (std::size_t k = 0; k < m_order.size(); ++k) {
            const std::size_t p = m_order[k];
            for (const std::size_t w : m_adjacent[p]) {
                const std::size_t q = from(w) == p ? to(w) : from(w);
                if (!reached[q]) {
                    reached[q] = true;
                    m_parentWall[q] = w;
                    m_inTree[w] = true;
                    m_depth[q] = m_depth[p] + 1;
                    m_order.push_back(q);
                }
            }
        }
    }

    [[nodiscard]] std::size_t parentOf(std::size_t p) const
    {
        const std::size_t w = m_parentWall[p];
        return from(w) == p ? to(w) : from(w);
    }

    // Fundamental cycles: each wall out of the tree, from its `to` point back
    // through the tree to its `from` point.
    void cells()
    {
        for (std::size_t w = 0; w < wallCount(); ++w) {
            if (m_inTree[w]) {
                continue;
            }
            std::vector<CellStep> up;   // from `to` up to the common ancestor
            std::vector<CellStep> down; // from `from` up, reversed later
            std::size_t a = to(w);
            std::size_t b = from(w);
            while (a != b) {
                if (m_depth[a] >= m_depth[b]) {
                    const std::size_t wall = m_parentWall[a];
                    up.push_back({.wall = wall, .sign = from(wall) == a ? 1.0 : -1.0});
                    a = parentOf(a);
                } else {
                    const std::size_t wall = m_parentWall[b];
                    // Walked later from the parent down to b.
                    down.push_back({.wall = wall, .sign = to(wall) == b ? 1.0 : -1.0});
                    b = parentOf(b);
                }
            }
            std::vector<CellStep> cell = std::move(up);
            cell.insert(cell.end(), down.rbegin(), down.rend());
            cell.push_back({.wall = w, .sign = 1.0});
            m_cells.push_back(std::move(cell));
        }
        m_inCell.assign(wallCount(), false);
        for (const auto& cell : m_cells) {
            for (const CellStep& step : cell) {
                m_inCell[step.wall] = true;
            }
        }
    }

    // Sign of a wall in a cell (0 when the cell does not use it).
    [[nodiscard]] double signIn(std::size_t cell, std::size_t wall) const
    {
        double sign = 0.0;
        for (const CellStep& step : m_cells[cell]) {
            if (step.wall == wall) {
                sign += step.sign;
            }
        }
        return sign;
    }

    // Generalised Bredt: the circulating flows of the cells for a unit twist
    // rate with G = 1, from the compatibility of each cell:
    // sum over d of q_d (integral over c and d of ds / t) = 2 A_c.
    void torsion()
    {
        const auto n = static_cast<Eigen::Index>(m_cells.size());
        m_flexibility = Eigen::MatrixXd::Zero(n, n);
        Eigen::VectorXd twiceArea = Eigen::VectorXd::Zero(n);
        for (Eigen::Index c = 0; c < n; ++c) {
            for (const CellStep& step : m_cells[static_cast<std::size_t>(c)]) {
                const std::size_t w = step.wall;
                const Vec a = local(step.sign > 0 ? from(w) : to(w));
                const Vec b = local(step.sign > 0 ? to(w) : from(w));
                twiceArea(c) += cross(a, b);
                for (Eigen::Index d = 0; d < n; ++d) {
                    m_flexibility(c, d) += step.sign * signIn(static_cast<std::size_t>(d), w) *
                                           length(w) / thickness(w);
                }
            }
        }
        m_cellFlow =
            n > 0 ? Eigen::VectorXd(m_flexibility.ldlt().solve(twiceArea)) : Eigen::VectorXd();
        m_torsion = n > 0 ? m_cellFlow.dot(twiceArea) : 0.0;
        // Saint-Venant's b t^3 / 3 of the open branches.
        for (std::size_t w = 0; w < wallCount(); ++w) {
            if (!m_inCell[w]) {
                m_torsion += length(w) * std::pow(thickness(w), 3) / 3.0;
            }
        }
        m_wallTorsionFlow.assign(wallCount(), 0.0);
        for (std::size_t c = 0; c < m_cells.size(); ++c) {
            for (const CellStep& step : m_cells[c]) {
                m_wallTorsionFlow[step.wall] +=
                    step.sign * m_cellFlow(static_cast<Eigen::Index>(c));
            }
        }
    }

    // The sectorial coordinate with the pole at the centroid, corrected in
    // the cells by the torsion flows (closed sections, Vlasov):
    // d omega = (y dz - z dy) - q / t ds. Then the shear centre (Trefftz) and
    // omega about it, normalised; Iw = integral of omega^2 dA.
    void sectorial()
    {
        std::vector<double> omega(pointCount(), 0.0);
        for (std::size_t k = 1; k < m_order.size(); ++k) {
            const std::size_t x = m_order[k];
            const std::size_t w = m_parentWall[x];
            const std::size_t p = parentOf(x);
            const double along = from(w) == p ? 1.0 : -1.0; // p to x along the wall
            omega[x] = omega[p] + cross(local(p), local(x)) -
                       along * m_wallTorsionFlow[w] * length(w) / thickness(w);
        }
        double sum = 0.0;
        for (std::size_t w = 0; w < wallCount(); ++w) {
            sum += length(w) * thickness(w) * (omega[from(w)] + omega[to(w)]) / 2.0;
        }
        const double mean = sum / m_geometry.area;
        for (double& value : omega) {
            value -= mean;
        }
        double omegaY = 0.0; // integral of omega y dA
        double omegaZ = 0.0;
        for (std::size_t w = 0; w < wallCount(); ++w) {
            const double dA = length(w) * thickness(w);
            const Vec a = local(from(w));
            const Vec b = local(to(w));
            omegaY += dA * linearProduct(omega[from(w)], omega[to(w)], a.x(), b.x());
            omegaZ += dA * linearProduct(omega[from(w)], omega[to(w)], a.y(), b.y());
        }
        // omega_S = omega - ys z + zs y, orthogonal to y and z.
        // A section on one straight line has no inertia across it: the
        // minimum-norm solution keeps its shear centre on the line.
        Eigen::Matrix2d m;
        m << -m_geometry.iyz, m_geometry.iz, -m_geometry.iy, m_geometry.iyz;
        m_shearCentre = m.completeOrthogonalDecomposition().solve(Vec(-omegaY, -omegaZ));
        m_omega.assign(pointCount(), 0.0);
        for (std::size_t p = 0; p < pointCount(); ++p) {
            if (m_adjacent[p].empty()) {
                continue;
            }
            const Vec r = local(p);
            m_omega[p] = omega[p] - m_shearCentre.x() * r.y() + m_shearCentre.y() * r.x();
        }
        m_warping = 0.0;
        for (std::size_t w = 0; w < wallCount(); ++w) {
            const double a = m_omega[from(w)];
            const double b = m_omega[to(w)];
            m_warping += length(w) * thickness(w) * (a * a + a * b + b * b) / 3.0;
        }
    }

    // The shear flow of a unit shear force along a principal axis (y' or z'),
    // open on the tree with the cell walls cut at their `to` end, closed by
    // circulating flows of no twist; As = V^2 / integral of q^2 / t ds.
    [[nodiscard]] double shearArea(double theta, bool alongY,
                                   std::vector<std::array<double, 2>>& endFlows) const
    {
        endFlows.assign(wallCount(), {0.0, 0.0});
        std::vector<double> f(pointCount(), 0.0); // the principal coordinate
        double inertia = 0.0;
        for (std::size_t p = 0; p < pointCount(); ++p) {
            const Vec r = toPrincipal(local(p), theta);
            f[p] = alongY ? r.x() : r.y();
        }
        for (std::size_t w = 0; w < wallCount(); ++w) {
            const double a = f[from(w)];
            const double b = f[to(w)];
            inertia += length(w) * thickness(w) * linearProduct(a, b, a, b);
        }
        if (!(inertia > 1e-14 * m_geometry.area * m_geometry.area)) {
            return 0.0; // no inertia across a straight section: no shear area
        }
        // Open flows, toward the inner end of each wall: q(s) = q_outer -
        // (t / I) integral of f ds from the outer end.
        std::vector<std::size_t> outer(wallCount());
        std::vector<double> outerFlow(wallCount(), 0.0);
        std::vector<double> inflow(pointCount(), 0.0);
        const auto flowAt = [&](std::size_t w, double s) {
            const std::size_t o = outer[w];
            const std::size_t i = o == from(w) ? to(w) : from(w);
            const double l = length(w);
            return outerFlow[w] -
                   thickness(w) / inertia * (f[o] * s + (f[i] - f[o]) * s * s / (2 * l));
        };
        for (std::size_t w = 0; w < wallCount(); ++w) {
            if (!m_inTree[w]) {
                outer[w] = to(w); // cut: free at its `to` end
                inflow[from(w)] += flowAt(w, length(w));
            }
        }
        for (std::size_t k = m_order.size(); k-- > 1;) {
            const std::size_t x = m_order[k];
            const std::size_t w = m_parentWall[x];
            outer[w] = x;
            outerFlow[w] = inflow[x];
            inflow[parentOf(x)] += flowAt(w, length(w));
        }
        // The open flow along `from` to `to`, at the fraction u of the wall.
        const auto open = [&](std::size_t w, double u) {
            const double l = length(w);
            return outer[w] == from(w) ? flowAt(w, u * l) : -flowAt(w, (1.0 - u) * l);
        };
        // Circulating flows: no twist in any cell.
        const auto n = static_cast<Eigen::Index>(m_cells.size());
        Eigen::VectorXd circulation;
        if (n > 0) {
            Eigen::VectorXd gap = Eigen::VectorXd::Zero(n);
            for (Eigen::Index c = 0; c < n; ++c) {
                for (const CellStep& step : m_cells[static_cast<std::size_t>(c)]) {
                    const std::size_t w = step.wall;
                    double integral = 0.0;
                    for (std::size_t g = 0; g < kGaussPoints.size(); ++g) {
                        integral += kGaussWeights.at(g) * open(w, kGaussPoints.at(g));
                    }
                    gap(c) += step.sign * integral * length(w) / thickness(w);
                }
            }
            circulation = m_flexibility.ldlt().solve(-gap);
        }
        double energy = 0.0;
        for (std::size_t w = 0; w < wallCount(); ++w) {
            double closing = 0.0;
            for (Eigen::Index c = 0; c < n; ++c) {
                closing += signIn(static_cast<std::size_t>(c), w) * circulation(c);
            }
            endFlows[w] = {open(w, 0.0) + closing, open(w, 1.0) + closing};
            double integral = 0.0;
            for (std::size_t g = 0; g < kGaussPoints.size(); ++g) {
                const double q = open(w, kGaussPoints.at(g)) + closing;
                integral += kGaussWeights.at(g) * q * q;
            }
            energy += integral * length(w) / thickness(w);
        }
        return 1.0 / energy;
    }

    const ThinWalled& m_shape;
    std::string m_prefix;
    SectionGeometry m_geometry;
    std::vector<std::vector<std::size_t>> m_adjacent; // walls per point
    std::vector<std::size_t> m_parentWall;            // per point, in the tree
    std::vector<int> m_depth;
    std::vector<bool> m_inTree;
    std::vector<std::size_t> m_order; // breadth-first
    std::vector<std::vector<CellStep>> m_cells;
    std::vector<bool> m_inCell;
    Eigen::MatrixXd m_flexibility;
    Eigen::VectorXd m_cellFlow;
    std::vector<double> m_wallTorsionFlow; // along `from` to `to`
    double m_torsion = 0.0;
    Vec m_shearCentre = Vec::Zero(); // from the centroid
    std::vector<double> m_omega;
    double m_warping = 0.0;
};

} // namespace

Expected<ThinWalledData> thinWalled(const ThinWalled& shape, std::string_view section)
{
    ThinWalledSolver solver(shape, section);
    return solver.solve();
}

} // namespace fem::core::detail
