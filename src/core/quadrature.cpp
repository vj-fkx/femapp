#include <fem/core/quadrature.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <numbers>
#include <vector>

namespace fem::core {

namespace {

constexpr int kMaxTensorDegree = 9;
constexpr int kMaxSimplexDegree = 5;
constexpr int kMaxGaussPoints = 5;

struct Gauss1d
{
    std::array<double, kMaxGaussPoints> x{};
    std::array<double, kMaxGaussPoints> w{};
    int n = 0;
};

// Gauss-Legendre on [-1, 1], n = 1 to 5 points (exact to degree 2n - 1).
Gauss1d gauss(int n)
{
    switch (n) {
    case 1:
        return {.x = {0.0}, .w = {2.0}, .n = 1};
    case 2:
        return {.x = {-std::numbers::inv_sqrt3, std::numbers::inv_sqrt3}, .w = {1.0, 1.0}, .n = 2};
    case 3:
        return {.x = {-0.77459666924148338, 0.0, 0.77459666924148338},
                .w = {0.55555555555555556, 0.88888888888888889, 0.55555555555555556},
                .n = 3};
    case 4:
        return {.x = {-0.86113631159405258, -0.33998104358485626, 0.33998104358485626,
                      0.86113631159405258},
                .w = {0.34785484513745386, 0.65214515486254614, 0.65214515486254614,
                      0.34785484513745386},
                .n = 4};
    default:
        return {.x = {-0.90617984593866399, -0.53846931010568309, 0.0, 0.53846931010568309,
                      0.90617984593866399},
                .w = {0.23692688505618909, 0.47862867049936647, 0.56888888888888889,
                      0.47862867049936647, 0.23692688505618909},
                .n = 5};
    }
}

int gaussPointsFor(int degree)
{
    return std::max(1, (degree + 2) / 2); // smallest n with 2n - 1 >= degree
}

using Rule = std::vector<QuadraturePoint>;

Rule tensorRule(int degree, int dims)
{
    const Gauss1d g = gauss(gaussPointsFor(degree));
    Rule rule;
    for (int i = 0; i < g.n; ++i) {
        for (int j = 0; j < (dims > 1 ? g.n : 1); ++j) {
            for (int k = 0; k < (dims > 2 ? g.n : 1); ++k) {
                const auto ii = static_cast<std::size_t>(i);
                const auto jj = static_cast<std::size_t>(j);
                const auto kk = static_cast<std::size_t>(k);
                QuadraturePoint p;
                p.xi = {g.x.at(ii), dims > 1 ? g.x.at(jj) : 0.0, dims > 2 ? g.x.at(kk) : 0.0};
                p.weight =
                    g.w.at(ii) * (dims > 1 ? g.w.at(jj) : 1.0) * (dims > 2 ? g.w.at(kk) : 1.0);
                rule.push_back(p);
            }
        }
    }
    return rule;
}

// Symmetric rules on the unit triangle, weights normalized to 1 then scaled
// by the area 1/2. Dunavant: degree 1 (1 point), 2 (3), 4 (6), 5 (7).
Rule triangleRule(int degree)
{
    Rule rule;
    const auto add = [&rule](double u, double v, double weight) {
        rule.push_back({.xi = {u, v, 0.0}, .weight = 0.5 * weight});
    };
    // The three points (a, a), (1 - 2a, a), (a, 1 - 2a).
    const auto orbit3 = [&add](double a, double weight) {
        add(a, a, weight);
        add(1.0 - 2.0 * a, a, weight);
        add(a, 1.0 - 2.0 * a, weight);
    };
    if (degree <= 1) {
        add(1.0 / 3.0, 1.0 / 3.0, 1.0);
    } else if (degree == 2) {
        orbit3(1.0 / 6.0, 1.0 / 3.0);
    } else if (degree <= 4) {
        orbit3(0.44594849091596489, 0.22338158967801147);
        orbit3(0.091576213509770743, 0.10995174365532187);
    } else {
        add(1.0 / 3.0, 1.0 / 3.0, 0.225);
        orbit3(0.47014206410511509, 0.13239415278850619);
        orbit3(0.10128650732345634, 0.12593918054482714);
    }
    return rule;
}

// Symmetric rules on the unit tetrahedron, weights normalized to 1 then
// scaled by the volume 1/6: degree 1 (1 point), 2 (4), 5 (14, Walkington).
Rule tetrahedronRule(int degree)
{
    Rule rule;
    // Barycentric (l0, l1, l2, l3); x = l1, y = l2, z = l3.
    const auto add = [&rule](const std::array<double, 4>& l, double weight) {
        rule.push_back({.xi = {l[1], l[2], l[3]}, .weight = weight / 6.0});
    };
    // The four points with three coordinates equal to a.
    const auto orbit4 = [&add](double a, double weight) {
        const double b = 1.0 - 3.0 * a;
        add({b, a, a, a}, weight);
        add({a, b, a, a}, weight);
        add({a, a, b, a}, weight);
        add({a, a, a, b}, weight);
    };
    // The six points with two coordinates equal to a and two to 1/2 - a.
    const auto orbit6 = [&add](double a, double weight) {
        const double b = 0.5 - a;
        add({a, a, b, b}, weight);
        add({a, b, a, b}, weight);
        add({a, b, b, a}, weight);
        add({b, a, a, b}, weight);
        add({b, a, b, a}, weight);
        add({b, b, a, a}, weight);
    };
    if (degree <= 1) {
        add({0.25, 0.25, 0.25, 0.25}, 1.0);
    } else if (degree == 2) {
        orbit4(0.13819660112501051, 0.25);
    } else {
        orbit4(0.092735250310891226, 0.073493043116361950);
        orbit4(0.31088591926330061, 0.11268792571801585);
        orbit6(0.045503704125649649, 0.042546020777081466);
    }
    return rule;
}

// Triangle x Gauss on [-1, 1].
Rule wedgeRule(int degree)
{
    const Rule triangle = triangleRule(degree);
    const Gauss1d g = gauss(gaussPointsFor(degree));
    Rule rule;
    for (const QuadraturePoint& t : triangle) {
        for (int k = 0; k < g.n; ++k) {
            const auto kk = static_cast<std::size_t>(k);
            rule.push_back({.xi = {t.xi[0], t.xi[1], g.x.at(kk)}, .weight = t.weight * g.w.at(kk)});
        }
    }
    return rule;
}

// Collapsed Gauss (Duffy): x = u (1 - w), y = v (1 - w), z = w, with the
// Jacobian (1 - w)^2 in the weights; w on [0, 1].
Rule pyramidRule(int degree)
{
    const Gauss1d g = gauss(gaussPointsFor(degree));
    const Gauss1d h = gauss(gaussPointsFor(degree + 2));
    Rule rule;
    for (int k = 0; k < h.n; ++k) {
        const auto kk = static_cast<std::size_t>(k);
        const double w = 0.5 * (h.x.at(kk) + 1.0);
        const double scale = (1.0 - w) * (1.0 - w) * 0.5 * h.w.at(kk);
        for (int i = 0; i < g.n; ++i) {
            for (int j = 0; j < g.n; ++j) {
                const auto ii = static_cast<std::size_t>(i);
                const auto jj = static_cast<std::size_t>(j);
                rule.push_back({.xi = {g.x.at(ii) * (1.0 - w), g.x.at(jj) * (1.0 - w), w},
                                .weight = g.w.at(ii) * g.w.at(jj) * scale});
            }
        }
    }
    return rule;
}

Rule makeRule(ElementFamily family, int degree)
{
    switch (family) {
    case ElementFamily::Point:
        return {{.xi = {0.0, 0.0, 0.0}, .weight = 1.0}};
    case ElementFamily::Line:
        return tensorRule(degree, 1);
    case ElementFamily::Quadrilateral:
        return tensorRule(degree, 2);
    case ElementFamily::Hexahedron:
        return tensorRule(degree, 3);
    case ElementFamily::Triangle:
        return triangleRule(degree);
    case ElementFamily::Tetrahedron:
        return tetrahedronRule(degree);
    case ElementFamily::Wedge:
        return wedgeRule(degree);
    case ElementFamily::Pyramid:
        return pyramidRule(degree);
    }
    return {};
}

constexpr std::size_t kFamilies = 8;

} // namespace

int maxQuadratureDegree(ElementFamily family) noexcept
{
    switch (family) {
    case ElementFamily::Point:
    case ElementFamily::Line:
    case ElementFamily::Quadrilateral:
    case ElementFamily::Hexahedron:
        return kMaxTensorDegree;
    default:
        return kMaxSimplexDegree;
    }
}

std::span<const QuadraturePoint> quadrature(ElementFamily family, int degree)
{
    // All rules are built once, on first use (thread safe).
    static const auto tables = [] {
        std::array<std::array<Rule, kMaxTensorDegree + 1>, kFamilies> all{};
        for (std::size_t f = 0; f < kFamilies; ++f) {
            const auto fam = static_cast<ElementFamily>(f);
            for (int d = 0; d <= maxQuadratureDegree(fam); ++d) {
                all.at(f).at(static_cast<std::size_t>(d)) = makeRule(fam, d);
            }
        }
        return all;
    }();
    assert(degree >= 0 && degree <= maxQuadratureDegree(family));
    const int d = degree < 0 ? 0 : std::min(degree, maxQuadratureDegree(family));
    return tables.at(static_cast<std::size_t>(family)).at(static_cast<std::size_t>(d));
}

} // namespace fem::core
