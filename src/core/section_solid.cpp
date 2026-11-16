// REQ-008b-02: properties of a solid polygon section from finite elements on
// its triangulation (design 008b §5.1.3; Pilkey, Analysis and Design of
// Elastic Beams, 2002, chapter 6):
// - Saint-Venant warping phi (u = theta' phi): Laplace(phi) = 0 with
//   d phi / dn = z n_y - y n_z, J = Iy + Iz - phi^T K phi;
// - the sectorial coordinate omega = -phi (u = -theta' omega, as in the
//   thin-walled sections), the shear centre of Trefftz and Iw about it;
// - the shear of a shear force with nu = 0: tau = (V / I) grad(chi), with
//   Laplace(chi) = -y' and d chi / dn = 0, As = I^2 / (chi^T K chi).

#include "detail/section_detail.hpp"

#include <fem/core/detail/section_mesh.hpp>

#include <Eigen/Core>
#include <Eigen/LU>
#include <Eigen/QR>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseCore>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace fem::core::detail {

namespace {

using Vec = Eigen::Vector2d;

// Dunavant's rule of degree 4 on the triangle: area coordinates and weights
// (the weights sum to 1).
struct TrianglePoint
{
    std::array<double, 3> l;
    double w;
};

constexpr double kA1 = 0.108103018168070;
constexpr double kB1 = 0.445948490915965;
constexpr double kW1 = 0.223381589678011;
constexpr double kA2 = 0.816847572980459;
constexpr double kB2 = 0.091576213509771;
constexpr double kW2 = 0.109951743655322;

constexpr std::array<TrianglePoint, 6> kPoints = {{
    {.l = {kA1, kB1, kB1}, .w = kW1},
    {.l = {kB1, kA1, kB1}, .w = kW1},
    {.l = {kB1, kB1, kA1}, .w = kW1},
    {.l = {kA2, kB2, kB2}, .w = kW2},
    {.l = {kB2, kA2, kB2}, .w = kW2},
    {.l = {kB2, kB2, kA2}, .w = kW2},
}};

} // namespace

SolidElement solidElement(const SectionMesh& mesh, std::size_t triangle)
{
    const auto& t = mesh.triangles[triangle];
    SolidElement e;
    for (std::size_t k = 0; k < 6; ++k) {
        e.nodes.at(k) = t.at(k);
    }
    const Vec& p0 = mesh.nodes[static_cast<std::size_t>(t[0])];
    const Vec& p1 = mesh.nodes[static_cast<std::size_t>(t[1])];
    const Vec& p2 = mesh.nodes[static_cast<std::size_t>(t[2])];
    const double twice = (p1 - p0).x() * (p2 - p0).y() - (p1 - p0).y() * (p2 - p0).x();
    e.area = twice / 2.0;
    e.corners = {p0, p1, p2};
    // Gradients of the area coordinates: grad L_i = (z_j - z_k, y_k - y_j) / 2A.
    for (std::size_t i = 0; i < 3; ++i) {
        const Vec& pj = e.corners.at((i + 1) % 3);
        const Vec& pk = e.corners.at((i + 2) % 3);
        e.gradL.at(i) = Vec(pj.y() - pk.y(), pk.x() - pj.x()) / twice;
    }
    return e;
}

SolidShape solidShape(const SolidElement& e, const std::array<double, 3>& l)
{
    SolidShape s;
    const std::array<std::pair<std::size_t, std::size_t>, 3> edges = {{{0, 1}, {1, 2}, {2, 0}}};
    for (std::size_t i = 0; i < 3; ++i) {
        s.n(static_cast<Eigen::Index>(i)) = l.at(i) * (2.0 * l.at(i) - 1.0);
        s.gradient.col(static_cast<Eigen::Index>(i)) = (4.0 * l.at(i) - 1.0) * e.gradL.at(i);
    }
    for (std::size_t k = 0; k < 3; ++k) {
        const auto [i, j] = edges.at(k);
        const auto c = static_cast<Eigen::Index>(3 + k);
        s.n(c) = 4.0 * l.at(i) * l.at(j);
        s.gradient.col(c) = 4.0 * (l.at(i) * e.gradL.at(j) + l.at(j) * e.gradL.at(i));
    }
    s.position = l[0] * e.corners[0] + l[1] * e.corners[1] + l[2] * e.corners[2];
    return s;
}

Expected<SolidData> solidSection(const SolidPolygon& shape, std::string_view section)
{
    auto meshed = meshSection(shape, section);
    if (!meshed) {
        return tl::unexpected(meshed.error());
    }
    auto field = std::make_shared<SolidField>();
    field->mesh = std::move(*meshed);
    const SectionMesh& mesh = field->mesh;
    const auto nodeCount = static_cast<Eigen::Index>(mesh.nodes.size());

    // Geometry, exact: the quadrature integrates the polynomials exactly on
    // straight triangles that cover the polygons.
    SolidData out;
    double area = 0.0;
    Vec first = Vec::Zero();
    double yy = 0.0;
    double zz = 0.0;
    double yz = 0.0;
    std::vector<SolidElement> elements;
    elements.reserve(mesh.triangles.size());
    for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
        elements.push_back(solidElement(mesh, t));
        const SolidElement& e = elements.back();
        for (const TrianglePoint& q : kPoints) {
            const Vec p = q.l[0] * e.corners[0] + q.l[1] * e.corners[1] + q.l[2] * e.corners[2];
            const double w = q.w * e.area;
            area += w;
            first += w * p;
            yy += w * p.x() * p.x();
            zz += w * p.y() * p.y();
            yz += w * p.x() * p.y();
        }
    }
    SectionGeometry& g = out.geometry;
    g.area = area;
    g.centroid = first / area;
    g.iz = yy - area * g.centroid.x() * g.centroid.x();
    g.iy = zz - area * g.centroid.y() * g.centroid.y();
    g.iyz = yz - area * g.centroid.x() * g.centroid.y();
    const double theta = principalAngle(g.iy, g.iz, g.iyz);
    field->centroid = g.centroid;
    field->principalAngle = theta;

    // The stiffness of the Laplacian and the right-hand sides, with the
    // coordinates from the centroid (drawing) and principal (y', z').
    std::vector<Eigen::Triplet<double>> entries;
    entries.reserve(mesh.triangles.size() * 36);
    Eigen::VectorXd warpingLoad = Eigen::VectorXd::Zero(nodeCount);
    Eigen::VectorXd loadY = Eigen::VectorXd::Zero(nodeCount);
    Eigen::VectorXd loadZ = Eigen::VectorXd::Zero(nodeCount);
    for (const SolidElement& e : elements) {
        Eigen::Matrix<double, 6, 6> k = Eigen::Matrix<double, 6, 6>::Zero();
        for (const TrianglePoint& q : kPoints) {
            const SolidShape s = solidShape(e, q.l);
            const double w = q.w * e.area;
            const Vec r = s.position - g.centroid;
            const Vec rp = toPrincipal(r, theta);
            k += w * s.gradient.transpose() * s.gradient;
            for (Eigen::Index i = 0; i < 6; ++i) {
                const auto node =
                    static_cast<Eigen::Index>(e.nodes.at(static_cast<std::size_t>(i)));
                warpingLoad(node) += w * (r.y() * s.gradient(0, i) - r.x() * s.gradient(1, i));
                loadY(node) += w * rp.x() * s.n(i);
                loadZ(node) += w * rp.y() * s.n(i);
            }
        }
        for (Eigen::Index i = 0; i < 6; ++i) {
            for (Eigen::Index j = 0; j < 6; ++j) {
                entries.emplace_back(e.nodes.at(static_cast<std::size_t>(i)),
                                     e.nodes.at(static_cast<std::size_t>(j)), k(i, j));
            }
        }
    }
    Eigen::SparseMatrix<double> stiffness(nodeCount, nodeCount);
    stiffness.setFromTriplets(entries.begin(), entries.end());
    // Neumann problems: their solutions are defined up to a constant; the
    // node 0 is held at zero (the loads are balanced), then normalised.
    const Eigen::SparseMatrix<double> held = [&stiffness] {
        Eigen::SparseMatrix<double> m = stiffness;
        for (Eigen::Index j = 0; j < m.outerSize(); ++j) {
            for (Eigen::SparseMatrix<double>::InnerIterator it(m, j); it; ++it) {
                if (it.row() == 0 || it.col() == 0) {
                    it.valueRef() = it.row() == it.col() ? 1.0 : 0.0;
                }
            }
        }
        return m;
    }();
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver(held);
    const auto solve = [&solver](Eigen::VectorXd load) {
        load(0) = 0.0;
        return Eigen::VectorXd(solver.solve(load));
    };
    const Eigen::VectorXd phi = solve(warpingLoad);
    const Eigen::VectorXd chiY = solve(loadY);
    const Eigen::VectorXd chiZ = solve(loadZ);

    out.torsion = g.iy + g.iz - phi.dot(stiffness * phi);
    const double inertiaZ = toPrincipalInertia(g, theta, true);
    const double inertiaY = toPrincipalInertia(g, theta, false);
    out.shearAreaY = inertiaZ * inertiaZ / chiY.dot(stiffness * chiY);
    out.shearAreaZ = inertiaY * inertiaY / chiZ.dot(stiffness * chiZ);

    // Sectorial coordinate about the centroid, normalised; Trefftz.
    Eigen::VectorXd omega = -phi;
    double mean = 0.0;
    double omegaY = 0.0;
    double omegaZ = 0.0;
    for (const SolidElement& e : elements) {
        for (const TrianglePoint& q : kPoints) {
            const SolidShape s = solidShape(e, q.l);
            double value = 0.0;
            for (Eigen::Index i = 0; i < 6; ++i) {
                value += s.n(i) * omega(e.nodes.at(static_cast<std::size_t>(i)));
            }
            mean += q.w * e.area * value;
        }
    }
    mean /= area;
    omega.array() -= mean;
    for (const SolidElement& e : elements) {
        for (const TrianglePoint& q : kPoints) {
            const SolidShape s = solidShape(e, q.l);
            const Vec r = s.position - g.centroid;
            double value = 0.0;
            for (Eigen::Index i = 0; i < 6; ++i) {
                value += s.n(i) * omega(e.nodes.at(static_cast<std::size_t>(i)));
            }
            omegaY += q.w * e.area * value * r.x();
            omegaZ += q.w * e.area * value * r.y();
        }
    }
    Eigen::Matrix2d m;
    m << -g.iyz, g.iz, -g.iy, g.iyz;
    const Vec shearCentre = m.completeOrthogonalDecomposition().solve(Vec(-omegaY, -omegaZ));
    for (Eigen::Index n = 0; n < nodeCount; ++n) {
        const Vec r = mesh.nodes[static_cast<std::size_t>(n)] - g.centroid;
        omega(n) += -shearCentre.x() * r.y() + shearCentre.y() * r.x();
    }
    double warping = 0.0;
    for (const SolidElement& e : elements) {
        for (const TrianglePoint& q : kPoints) {
            const SolidShape s = solidShape(e, q.l);
            double value = 0.0;
            for (Eigen::Index i = 0; i < 6; ++i) {
                value += s.n(i) * omega(e.nodes.at(static_cast<std::size_t>(i)));
            }
            warping += q.w * e.area * value * value;
        }
    }
    out.shearCentre = g.centroid + shearCentre;
    out.warping = warping;
    field->phi = phi;
    field->omega = std::move(omega);
    field->chiY = chiY;
    field->chiZ = chiZ;
    out.field = std::move(field);
    return out;
}

double toPrincipalInertia(const SectionGeometry& g, double theta, bool aboutZ)
{
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    return aboutZ ? c * c * g.iz + s * s * g.iy + 2.0 * s * c * g.iyz
                  : s * s * g.iz + c * c * g.iy - 2.0 * s * c * g.iyz;
}

} // namespace fem::core::detail
