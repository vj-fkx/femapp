#include "detail/section_detail.hpp"

#include <fem/core/section.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fem::core {

namespace {

constexpr double kPi = std::numbers::pi;

// Terms of the Saint-Venant series of the rectangle: the first neglected
// term is below 1e-9 of J.
constexpr int kTorsionSeriesTerms = 50;

// A rectangle of the drawing, added (sign 1) or removed (sign -1).
struct Part
{
    double y0 = 0.0;
    double y1 = 0.0;
    double z0 = 0.0;
    double z1 = 0.0;
    double sign = 1.0;
};

Part rect(double y0, double y1, double z0, double z1, double sign)
{
    return {.y0 = y0, .y1 = y1, .z0 = z0, .z1 = z1, .sign = sign};
}

// Area, centroid and second moments about the centroid, in the drawing.
struct Geometric
{
    double area = 0.0;
    Eigen::Vector2d centroid = Eigen::Vector2d::Zero();
    double iy = 0.0;  // integral of z^2
    double iz = 0.0;  // integral of y^2
    double iyz = 0.0; // integral of y z
};

Geometric combine(const std::vector<Part>& parts)
{
    double area = 0.0;
    double sy = 0.0; // integral of y
    double sz = 0.0;
    double yy = 0.0;
    double zz = 0.0;
    double yz = 0.0;
    for (const Part& p : parts) {
        const double dy = p.y1 - p.y0;
        const double dz = p.z1 - p.z0;
        area += p.sign * dy * dz;
        sy += p.sign * dz * (p.y1 * p.y1 - p.y0 * p.y0) / 2.0;
        sz += p.sign * dy * (p.z1 * p.z1 - p.z0 * p.z0) / 2.0;
        yy += p.sign * dz * (std::pow(p.y1, 3) - std::pow(p.y0, 3)) / 3.0;
        zz += p.sign * dy * (std::pow(p.z1, 3) - std::pow(p.z0, 3)) / 3.0;
        yz += p.sign * (p.y1 * p.y1 - p.y0 * p.y0) * (p.z1 * p.z1 - p.z0 * p.z0) / 4.0;
    }
    Geometric g;
    g.area = area;
    g.centroid = Eigen::Vector2d(sy / area, sz / area);
    g.iz = yy - area * g.centroid.x() * g.centroid.x();
    g.iy = zz - area * g.centroid.y() * g.centroid.y();
    g.iyz = yz - area * g.centroid.x() * g.centroid.y();
    return g;
}

// Saint-Venant torsion of a solid rectangle of sides `a` and `b`
// (Timoshenko and Goodier, Theory of Elasticity, §109):
// J = a c^3 / 3 [1 - 192 c / (pi^5 a) sum over odd n of tanh(n pi a / 2c) / n^5],
// with a the longer side and c the shorter.
double rectangleTorsion(double a, double b)
{
    const double longer = std::max(a, b);
    const double shorter = std::min(a, b);
    double sum = 0.0;
    for (int k = 0; k < kTorsionSeriesTerms; ++k) {
        const double n = 2.0 * k + 1.0;
        sum += std::tanh(n * kPi * longer / (2.0 * shorter)) / std::pow(n, 5);
    }
    return longer * std::pow(shorter, 3) / 3.0 *
           (1.0 - 192.0 * shorter / (std::pow(kPi, 5) * longer) * sum);
}

// Open thin-walled section: the sum of b t^3 / 3 over its plates
// (Young and Budynas, Roark's Formulas for Stress and Strain, table 10.7).
double thinPlate(double length, double thickness)
{
    return length * std::pow(thickness, 3) / 3.0;
}

// What a shape adds to the geometric properties.
struct ShapeData
{
    // In the order that leaves the least padding.
    Eigen::Vector2d shearCentre = Eigen::Vector2d::Zero(); // drawing, absolute
    // Exact values for the circular shapes, which are not rectangles.
    std::optional<Geometric> exact;
    double torsion = 0.0;
    double shearAreaY = 0.0;
    double shearAreaZ = 0.0;
    double warping = 0.0;                            // Iw (design 008b section 5.1.1)
    std::shared_ptr<const detail::SolidField> field; // solid polygon
    std::vector<Part> parts;
    bool shearCentreAtCentroid = true;
    // The shear areas are already about the principal axes (thin-walled,
    // solid polygon).
    bool principalShearAreas = false;
};

// REQ-008b-01: warping of the thin-walled shapes on their mid-lines
// (Pilkey, 2002, table 6; Galambos, 1968). With omega = y z on the flanges,
// the doubly symmetric I has Iw = tf bf^3 h0^2 / 24.
double iWarping(double bf, double tf, double h0)
{
    return tf * std::pow(bf, 3) * h0 * h0 / 24.0;
}

// Channel with flanges b (from the web mid-line), web h0 between the flange
// mid-lines and the shear centre e behind the web: omega is -e h0/2 at the
// top junction and (b - e) h0/2 at the top tip, antisymmetric below.
double channelWarping(double b, double tf, double h0, double tw, double e)
{
    return 2.0 * (tw * e * e * std::pow(h0, 3) / 24.0 +
                  tf * h0 * h0 / 4.0 * (std::pow(b - e, 3) + std::pow(e, 3)) / 3.0);
}

// Closed box on the mid-lines (b0 x h0, wall t): the normalised sectorial
// coordinate is zero at mid-sides and b0 h0 (h0 - b0) / (4 (b0 + h0)) at the
// corners where y z > 0 (the opposite where y z < 0).
double boxWarping(double b0, double h0, double t)
{
    return t * b0 * b0 * h0 * h0 * std::pow(b0 - h0, 2) / (24.0 * (b0 + h0));
}

double boxCornerSectorial(double b0, double h0)
{
    return b0 * h0 * (h0 - b0) / (4.0 * (b0 + h0));
}

// Shear centre of the channel behind the web mid-line (Roark, table 10.1).
double channelShearCentre(double b, double tf, double h0, double tw)
{
    return 3.0 * b * b * tf / (6.0 * b * tf + h0 * tw);
}

class ShapeBuilder
{
public:
    explicit ShapeBuilder(std::string_view section)
        : m_section(section)
    {}

    Expected<ShapeData> operator()(const Rectangle& s) const
    {
        if (auto bad = positive({{"b", s.b}, {"h", s.h}})) {
            return makeError(*bad);
        }
        ShapeData d;
        d.parts = {rect(-s.h / 2, s.h / 2, -s.b / 2, s.b / 2, 1.0)};
        d.torsion = rectangleTorsion(s.b, s.h);
        // Timoshenko shear coefficient 5/6 of the rectangle.
        d.shearAreaY = d.shearAreaZ = 5.0 / 6.0 * s.b * s.h;
        // Vlasov's warping of a plate, small and documented (design 008b 5.1.1).
        d.warping = std::pow(s.b * s.h, 3) / 144.0;
        return d;
    }

    Expected<ShapeData> operator()(const Circle& s) const
    {
        if (auto bad = positive({{"d", s.d}})) {
            return makeError(*bad);
        }
        ShapeData d;
        const double area = kPi * s.d * s.d / 4.0;
        const double inertia = kPi * std::pow(s.d, 4) / 64.0;
        d.exact = Geometric{
            .area = area, .centroid = Eigen::Vector2d::Zero(), .iy = inertia, .iz = inertia};
        d.torsion = 2.0 * inertia;
        // Shear coefficient 0.9 of the solid circle (Cowper gives 0.886 for nu = 0.3).
        d.shearAreaY = d.shearAreaZ = 0.9 * area;
        return d;
    }

    Expected<ShapeData> operator()(const CircularTube& s) const
    {
        if (auto bad = positive({{"d", s.d}, {"t", s.t}})) {
            return makeError(*bad);
        }
        if (!(2.0 * s.t < s.d)) {
            return makeError(prefix() + "the wall t must be less than d / 2.");
        }
        ShapeData d;
        const double inner = s.d - 2.0 * s.t;
        const double area = kPi * (s.d * s.d - inner * inner) / 4.0;
        const double inertia = kPi * (std::pow(s.d, 4) - std::pow(inner, 4)) / 64.0;
        d.exact = Geometric{
            .area = area, .centroid = Eigen::Vector2d::Zero(), .iy = inertia, .iz = inertia};
        d.torsion = 2.0 * inertia;
        // Thin tube: shear coefficient 1/2.
        d.shearAreaY = d.shearAreaZ = 0.5 * area;
        return d;
    }

    Expected<ShapeData> operator()(const RectangularTube& s) const
    {
        if (auto bad = positive({{"b", s.b}, {"h", s.h}, {"t", s.t}})) {
            return makeError(*bad);
        }
        if (!(2.0 * s.t < s.b) || !(2.0 * s.t < s.h)) {
            return makeError(prefix() + "the wall t must be less than b / 2 and h / 2.");
        }
        ShapeData d;
        const double hb = s.b / 2;
        const double hh = s.h / 2;
        d.parts = {rect(-hh, hh, -hb, hb, 1.0),
                   rect(-hh + s.t, hh - s.t, -hb + s.t, hb - s.t, -1.0)};
        // Bredt, on the mid-line: J = 4 Am^2 t / pm.
        const double bm = s.b - s.t;
        const double hm = s.h - s.t;
        d.torsion = 4.0 * std::pow(bm * hm, 2) * s.t / (2.0 * (bm + hm));
        d.shearAreaY = 2.0 * hm * s.t; // the two walls along y
        d.shearAreaZ = 2.0 * bm * s.t;
        d.warping = boxWarping(bm, hm, s.t);
        return d;
    }

    Expected<ShapeData> operator()(const ISection& s) const
    {
        if (auto bad = positive({{"h", s.h}, {"bf", s.bf}, {"tf", s.tf}, {"tw", s.tw}})) {
            return makeError(*bad);
        }
        if (auto bad = flanged(s.h, s.bf, s.tf, s.tw, 2.0)) {
            return makeError(*bad);
        }
        ShapeData d;
        const double hh = s.h / 2;
        const double hb = s.bf / 2;
        d.parts = {rect(hh - s.tf, hh, -hb, hb, 1.0), rect(-hh, -hh + s.tf, -hb, hb, 1.0),
                   rect(-hh + s.tf, hh - s.tf, -s.tw / 2, s.tw / 2, 1.0)};
        d.torsion = 2.0 * thinPlate(s.bf, s.tf) + thinPlate(s.h - 2.0 * s.tf, s.tw);
        d.shearAreaY = (s.h - s.tf) * s.tw;
        d.shearAreaZ = 5.0 / 6.0 * 2.0 * s.bf * s.tf;
        d.warping = iWarping(s.bf, s.tf, s.h - s.tf);
        return d;
    }

    Expected<ShapeData> operator()(const Angle& s) const
    {
        if (auto bad = positive({{"b", s.b}, {"h", s.h}, {"t", s.t}})) {
            return makeError(*bad);
        }
        if (!(s.t < s.b) || !(s.t < s.h)) {
            return makeError(prefix() + "the thickness t must be less than b and h.");
        }
        ShapeData d;
        d.parts = {rect(0.0, s.h, 0.0, s.t, 1.0), rect(0.0, s.t, s.t, s.b, 1.0)};
        d.torsion = thinPlate(s.h, s.t) + thinPlate(s.b - s.t, s.t);
        // Where the mid-lines of the legs meet.
        d.shearCentre = Eigen::Vector2d(s.t / 2, s.t / 2);
        d.shearCentreAtCentroid = false;
        d.shearAreaY = 5.0 / 6.0 * s.h * s.t;
        d.shearAreaZ = 5.0 / 6.0 * s.b * s.t;
        return d;
    }

    Expected<ShapeData> operator()(const Channel& s) const
    {
        if (auto bad = positive({{"h", s.h}, {"bf", s.bf}, {"tf", s.tf}, {"tw", s.tw}})) {
            return makeError(*bad);
        }
        if (auto bad = flanged(s.h, s.bf, s.tf, s.tw, 2.0)) {
            return makeError(*bad);
        }
        ShapeData d;
        const double hh = s.h / 2;
        d.parts = {rect(-hh, hh, 0.0, s.tw, 1.0), rect(hh - s.tf, hh, s.tw, s.bf, 1.0),
                   rect(-hh, -hh + s.tf, s.tw, s.bf, 1.0)};
        d.torsion = 2.0 * thinPlate(s.bf - s.tw, s.tf) + thinPlate(s.h, s.tw);
        // Thin-walled channel on the mid-lines (Roark, table 10.1): the shear
        // centre is e = 3 b^2 tf / (6 b tf + h tw) behind the web, with b the
        // flange from the web mid-line and h between the flange mid-lines.
        const double b = s.bf - s.tw / 2;
        const double h = s.h - s.tf;
        const double e = channelShearCentre(b, s.tf, h, s.tw);
        d.shearCentre = Eigen::Vector2d(0.0, s.tw / 2 - e);
        d.shearCentreAtCentroid = false;
        d.shearAreaY = (s.h - s.tf) * s.tw;
        d.shearAreaZ = 5.0 / 6.0 * 2.0 * s.bf * s.tf;
        d.warping = channelWarping(b, s.tf, h, s.tw, e);
        return d;
    }

    Expected<ShapeData> operator()(const Tee& s) const
    {
        if (auto bad = positive({{"h", s.h}, {"bf", s.bf}, {"tf", s.tf}, {"tw", s.tw}})) {
            return makeError(*bad);
        }
        if (auto bad = flanged(s.h, s.bf, s.tf, s.tw, 1.0)) {
            return makeError(*bad);
        }
        ShapeData d;
        d.parts = {rect(s.h - s.tf, s.h, -s.bf / 2, s.bf / 2, 1.0),
                   rect(0.0, s.h - s.tf, -s.tw / 2, s.tw / 2, 1.0)};
        d.torsion = thinPlate(s.bf, s.tf) + thinPlate(s.h - s.tf, s.tw);
        // Where the mid-lines of the flange and of the stem meet.
        d.shearCentre = Eigen::Vector2d(s.h - s.tf / 2, 0.0);
        d.shearCentreAtCentroid = false;
        d.shearAreaY = (s.h - s.tf) * s.tw;
        d.shearAreaZ = 5.0 / 6.0 * s.bf * s.tf;
        return d;
    }

    Expected<ShapeData> operator()(const ThinWalled& s) const
    {
        const auto thin = detail::thinWalled(s, m_section);
        if (!thin) {
            return tl::unexpected(thin.error());
        }
        const detail::SectionGeometry& g = thin->geometry;
        ShapeData d;
        d.exact =
            Geometric{.area = g.area, .centroid = g.centroid, .iy = g.iy, .iz = g.iz, .iyz = g.iyz};
        d.torsion = thin->torsion;
        d.shearCentre = thin->shearCentre;
        d.shearCentreAtCentroid = false;
        d.shearAreaY = thin->shearAreaY;
        d.shearAreaZ = thin->shearAreaZ;
        d.principalShearAreas = true;
        d.warping = thin->warping;
        return d;
    }

    Expected<ShapeData> operator()(const SolidPolygon& s) const
    {
        auto solid = detail::solidSection(s, m_section);
        if (!solid) {
            return tl::unexpected(solid.error());
        }
        const detail::SectionGeometry& g = solid->geometry;
        ShapeData d;
        d.exact =
            Geometric{.area = g.area, .centroid = g.centroid, .iy = g.iy, .iz = g.iz, .iyz = g.iyz};
        d.torsion = solid->torsion;
        d.shearCentre = solid->shearCentre;
        d.shearCentreAtCentroid = false;
        d.shearAreaY = solid->shearAreaY;
        d.shearAreaZ = solid->shearAreaZ;
        d.principalShearAreas = true;
        d.warping = solid->warping;
        d.field = std::move(solid->field);
        return d;
    }

private:
    // A name and its value.
    using Dimension = std::pair<std::string_view, double>;

    [[nodiscard]] std::string prefix() const
    {
        return "Section \"" + std::string(m_section) + "\": ";
    }

    [[nodiscard]] std::optional<std::string> positive(std::initializer_list<Dimension> dims) const
    {
        for (const Dimension& dim : dims) {
            if (!(dim.second > 0.0) || !std::isfinite(dim.second)) {
                return prefix() + "the dimension " + std::string(dim.first) + " must be positive.";
            }
        }
        return std::nullopt;
    }

    // `flanges` flanges of thickness tf in the height h; the web tw fits in bf.
    [[nodiscard]] std::optional<std::string> flanged(double h, double bf, double tf, double tw,
                                                     double flanges) const
    {
        if (!(flanges * tf < h)) {
            return prefix() +
                   (flanges > 1.0 ? "2 tf must be less than h." : "tf must be less than h.");
        }
        if (!(tw <= bf)) {
            return prefix() + "tw must not exceed bf.";
        }
        return std::nullopt;
    }

    std::string_view m_section;
};

} // namespace

double detail::principalAngle(double iy, double iz, double iyz)
{
    const double scale = std::max({std::abs(iy), std::abs(iz), 1e-300});
    if (std::abs(iyz) <= 1e-14 * scale) {
        return 0.0;
    }
    double theta = 0.5 * std::atan2(2.0 * iyz, iz - iy);
    if (theta > kPi / 4) {
        theta -= kPi / 2;
    } else if (theta <= -kPi / 4) {
        theta += kPi / 2;
    }
    return theta;
}

Eigen::Vector2d detail::toPrincipal(const Eigen::Vector2d& v, double theta)
{
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    return {c * v.x() + s * v.y(), -s * v.x() + c * v.y()};
}

namespace {

using detail::principalAngle;
using detail::toPrincipal;

// Inertias, shear centre and shear areas in the principal axes.
SectionProperties principal(const Geometric& g, double torsion, const Eigen::Vector2d& shearCentre,
                            std::optional<double> shearAreaY, std::optional<double> shearAreaZ,
                            double warping)
{
    SectionProperties p;
    p.warpingConstant = warping;
    p.area = g.area;
    p.centroid = g.centroid;
    p.torsionConstant = torsion;
    const double theta = principalAngle(g.iy, g.iz, g.iyz);
    p.principalAngle = theta;
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    // I'z = integral of y'^2 with y' = c y + s z.
    p.inertiaZ = c * c * g.iz + s * s * g.iy + 2.0 * s * c * g.iyz;
    p.inertiaY = s * s * g.iz + c * c * g.iy - 2.0 * s * c * g.iyz;
    p.shearCentre = toPrincipal(shearCentre, theta);
    if (shearAreaY && shearAreaZ) {
        // The shear flexibility 1/As turns as a tensor.
        const double fy = 1.0 / *shearAreaY;
        const double fz = 1.0 / *shearAreaZ;
        p.shearAreaY = 1.0 / (c * c * fy + s * s * fz);
        p.shearAreaZ = 1.0 / (s * s * fy + c * c * fz);
    } else if (theta == 0.0) {
        p.shearAreaY = shearAreaY;
        p.shearAreaZ = shearAreaZ;
    }
    return p;
}

} // namespace

namespace {

// The properties of a section and, for a solid polygon, its fields.
struct Solved
{
    SectionProperties properties;
    std::shared_ptr<const detail::SolidField> field;
};

Expected<Solved> solve(const Section& section)
{
    if (!section.shape) {
        Geometric g;
        g.area = section.area.value_or(0.0);
        g.iy = section.inertiaY.value_or(0.0);
        g.iz = section.inertiaZ.value_or(0.0);
        g.iyz = section.productOfInertia.value_or(0.0);
        return Solved{.properties = principal(g, section.torsionConstant.value_or(0.0),
                                              section.shearCentre.value_or(Eigen::Vector2d::Zero()),
                                              section.shearAreaY, section.shearAreaZ,
                                              section.warpingConstant.value_or(0.0)),
                      .field = nullptr};
    }
    const auto data = std::visit(ShapeBuilder(section.name), *section.shape);
    if (!data) {
        return tl::unexpected(data.error());
    }
    const std::optional<Geometric>& exact = data->exact;
    const Geometric g = exact ? *exact : combine(data->parts);
    const Eigen::Vector2d shearCentre = data->shearCentreAtCentroid
                                            ? Eigen::Vector2d::Zero().eval()
                                            : (data->shearCentre - g.centroid).eval();
    SectionProperties p =
        principal(g, data->torsion, shearCentre, data->shearAreaY, data->shearAreaZ, data->warping);
    if (data->principalShearAreas) {
        p.shearAreaY = data->shearAreaY;
        p.shearAreaZ = data->shearAreaZ;
    }
    return Solved{.properties = p, .field = data->field};
}

// Points in the drawing; turned to the principal axes by stressPoints().
class PointBuilder
{
public:
    explicit PointBuilder(const SectionProperties& p)
        : m_p(p)
    {}

    std::vector<StressPoint> operator()(const Rectangle& s)
    {
        corners(s.h / 2, s.b / 2, 0.0);
        add("centre", 0.0, 0.0, s.b * s.h * s.h / 8, s.b, s.h * s.b * s.b / 8, s.h, 0.0);
        // Saint-Venant torsion of a rectangle 2a x 2b, a >= b: the largest
        // shear at the middle of the long sides (Roark, table 10.7, case 4):
        // tau = 3T / (8ab^2) [1 + 0.6095 r + 0.8865 r^2 - 1.8023 r^3 + 0.9100 r^4].
        const double a = std::max(s.b, s.h) / 2;
        const double b = std::min(s.b, s.h) / 2;
        const double r = b / a;
        const double factor =
            3.0 / (8.0 * a * b * b) *
            (1.0 + 0.6095 * r + 0.8865 * r * r - 1.8023 * r * r * r + 0.9100 * r * r * r * r);
        if (s.h >= s.b) {
            add("middle of the side +z", 0.0, s.b / 2, s.b * s.h * s.h / 8, s.b, 0.0, 0.0, factor);
        } else {
            add("middle of the side +y", s.h / 2, 0.0, 0.0, 0.0, s.h * s.b * s.b / 8, s.h, factor);
        }
        return std::move(m_points);
    }

    std::vector<StressPoint> operator()(const Circle& s)
    {
        perimeter(s.d / 2, std::pow(s.d, 3) / 12, s.d);
        return std::move(m_points);
    }

    std::vector<StressPoint> operator()(const CircularTube& s)
    {
        const double inner = s.d - 2 * s.t;
        perimeter(s.d / 2, (std::pow(s.d, 3) - std::pow(inner, 3)) / 12, 2 * s.t);
        return std::move(m_points);
    }

    std::vector<StressPoint> operator()(const RectangularTube& s)
    {
        // Bredt: tau = T / (2 Am t) all around.
        const double factor = 1.0 / (2.0 * (s.b - s.t) * (s.h - s.t) * s.t);
        corners(s.h / 2, s.b / 2, factor, boxCornerSectorial(s.b - s.t, s.h - s.t));
        const double qy = (s.b * s.h * s.h - (s.b - 2 * s.t) * std::pow(s.h - 2 * s.t, 2)) / 8;
        const double qz = (s.h * s.b * s.b - (s.h - 2 * s.t) * std::pow(s.b - 2 * s.t, 2)) / 8;
        add("middle of the side +z", 0.0, s.b / 2, qy, 2 * s.t, 0.0, 0.0, factor);
        add("middle of the side +y", s.h / 2, 0.0, 0.0, 0.0, qz, 2 * s.t, factor);
        return std::move(m_points);
    }

    std::vector<StressPoint> operator()(const ISection& s)
    {
        const double j = m_p.torsionConstant;
        corners(s.h / 2, s.bf / 2, s.tf / j, (s.h - s.tf) * s.bf / 4.0);
        const double qy = s.bf * s.tf * (s.h - s.tf) / 2 + s.tw * std::pow(s.h / 2 - s.tf, 2) / 2;
        add("web centre", 0.0, 0.0, qy, s.tw, 0.0, 0.0, s.tw / j);
        add("top flange centre", s.h / 2 - s.tf / 2, 0.0, 0.0, 0.0, s.tf * s.bf * s.bf / 8, s.tf,
            s.tf / j);
        return std::move(m_points);
    }

    std::vector<StressPoint> operator()(const Angle& s)
    {
        const double factor = s.t / m_p.torsionConstant;
        add("heel", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, factor);
        add("tip of the leg along y, outer", s.h, 0.0, 0.0, 0.0, 0.0, 0.0, factor);
        add("tip of the leg along y, inner", s.h, s.t, 0.0, 0.0, 0.0, 0.0, factor);
        add("tip of the leg along z, outer", 0.0, s.b, 0.0, 0.0, 0.0, 0.0, factor);
        add("tip of the leg along z, inner", s.t, s.b, 0.0, 0.0, 0.0, 0.0, factor);
        return std::move(m_points);
    }

    std::vector<StressPoint> operator()(const Channel& s)
    {
        const double j = m_p.torsionConstant;
        const double b = s.bf - s.tw / 2;
        const double h0 = s.h - s.tf;
        const double e = channelShearCentre(b, s.tf, h0, s.tw);
        for (const double y : {s.h / 2, -s.h / 2}) {
            const std::string side = y > 0 ? "+y" : "-y";
            const double sign = y > 0 ? 1.0 : -1.0;
            add("flange tip " + side, y, s.bf, 0.0, 0.0, 0.0, 0.0, s.tf / j,
                sign * (b - e) * h0 / 2);
            add("web back " + side, y, 0.0, 0.0, 0.0, 0.0, 0.0, s.tw / j, -sign * e * h0 / 2);
        }
        const double qy = s.bf * s.tf * (s.h - s.tf) / 2 + s.tw * std::pow(s.h / 2 - s.tf, 2) / 2;
        add("web centre", 0.0, s.tw / 2, qy, s.tw, 0.0, 0.0, s.tw / j);
        return std::move(m_points);
    }

    // REQ-008b-06: both faces of each wall at its two ends. tau = V q / t
    // with the flow q of a unit shear force, written as Q = |q| I; the
    // torsion: T t / J on the open branches, T q / (J t) in the cells.
    std::vector<StressPoint> operator()(const ThinWalled& s)
    {
        const auto thin = detail::thinWalled(s, "");
        if (!thin) {
            return {};
        }
        const double j = m_p.torsionConstant;
        for (std::size_t w = 0; w < s.walls.size(); ++w) {
            const ThinWalled::Wall& wall = s.walls[w];
            const std::array<int, 2> ends = {wall.from, wall.to};
            const Eigen::Vector2d a = s.points[static_cast<std::size_t>(wall.from)];
            const Eigen::Vector2d b = s.points[static_cast<std::size_t>(wall.to)];
            const Eigen::Vector2d d = (b - a).normalized();
            const Eigen::Vector2d normal(-d.y(), d.x());
            const double torsion =
                thin->inCell[w] ? std::abs(thin->torsionFlow[w]) / (j * wall.t) : wall.t / j;
            for (std::size_t e = 0; e < 2; ++e) {
                const auto point = static_cast<std::size_t>(ends.at(e));
                const Eigen::Vector2d p = s.points[point];
                for (const double side : {1.0, -1.0}) {
                    const Eigen::Vector2d q = p + side * wall.t / 2 * normal;
                    add("wall " + std::to_string(w) + (e == 0 ? " start" : " end") +
                            (side > 0 ? ", face +" : ", face -"),
                        q.x(), q.y(), std::abs(thin->shearFlowY[w].at(e)) * m_p.inertiaZ, wall.t,
                        std::abs(thin->shearFlowZ[w].at(e)) * m_p.inertiaY, wall.t, torsion,
                        thin->sectorial[point]);
                }
            }
        }
        return std::move(m_points);
    }

    // REQ-008b-06: the vertices; their shear comes from the fields of the
    // solution (sectionStresses).
    std::vector<StressPoint> operator()(const SolidPolygon& s)
    {
        for (std::size_t r = 0; r < s.regions.size(); ++r) {
            const auto& region = s.regions[r];
            const std::string where = " of the region " + std::to_string(r);
            for (std::size_t k = 0; k < region.outline.size(); ++k) {
                add("vertex " + std::to_string(k) + " of the outline" + where,
                    region.outline[k].x(), region.outline[k].y(), 0.0, 0.0, 0.0, 0.0, 0.0);
            }
            for (std::size_t h = 0; h < region.holes.size(); ++h) {
                for (std::size_t k = 0; k < region.holes[h].size(); ++k) {
                    add("vertex " + std::to_string(k) + " of the hole " + std::to_string(h) + where,
                        region.holes[h][k].x(), region.holes[h][k].y(), 0.0, 0.0, 0.0, 0.0, 0.0);
                }
            }
        }
        return std::move(m_points);
    }

    std::vector<StressPoint> operator()(const Tee& s)
    {
        const double j = m_p.torsionConstant;
        add("flange tip +z", s.h, s.bf / 2, 0.0, 0.0, 0.0, 0.0, s.tf / j);
        add("flange tip -z", s.h, -s.bf / 2, 0.0, 0.0, 0.0, 0.0, s.tf / j);
        add("stem bottom", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, s.tw / j);
        const double yc = m_p.centroid.x();
        if (yc < s.h - s.tf) {
            const double qy =
                s.bf * s.tf * (s.h - s.tf / 2 - yc) + s.tw * std::pow(s.h - s.tf - yc, 2) / 2;
            add("stem at the neutral axis", yc, 0.0, qy, s.tw, 0.0, 0.0, s.tw / j);
        } else {
            add("flange at the neutral axis", yc, 0.0, s.bf * std::pow(s.h - yc, 2) / 2, s.bf, 0.0,
                0.0, s.tf / j);
        }
        return std::move(m_points);
    }

private:
    void add(std::string name, double y, double z, double qy, double ty, double qz, double tz,
             double torsion, double sectorial = 0.0)
    {
        m_points.push_back(
            {.name = std::move(name),
             .position = toPrincipal(Eigen::Vector2d(y, z) - m_p.centroid, m_p.principalAngle),
             .firstMomentY = qy,
             .thicknessY = ty,
             .firstMomentZ = qz,
             .thicknessZ = tz,
             .torsionFactor = torsion,
             .sectorial = sectorial});
    }

    // The four corners of a centred box; omega is +sectorial where y z > 0.
    void corners(double y, double z, double torsion, double sectorial = 0.0)
    {
        add("corner +y +z", y, z, 0.0, 0.0, 0.0, 0.0, torsion, sectorial);
        add("corner +y -z", y, -z, 0.0, 0.0, 0.0, 0.0, torsion, -sectorial);
        add("corner -y +z", -y, z, 0.0, 0.0, 0.0, 0.0, torsion, -sectorial);
        add("corner -y -z", -y, -z, 0.0, 0.0, 0.0, 0.0, torsion, sectorial);
    }

    // Eight points on a circle of radius r; Q and t of the half section at
    // the neutral axis of each shear force.
    void perimeter(double r, double q, double t)
    {
        const double torsion = r / m_p.torsionConstant;
        for (int k = 0; k < 8; ++k) {
            const double angle = kPi / 4 * k;
            const double y = r * std::cos(angle);
            const double z = r * std::sin(angle);
            const bool onZ = k == 2 || k == 6; // y = 0: shear of Vy
            const bool onY = k == 0 || k == 4; // z = 0: shear of Vz
            add("perimeter at " + std::to_string(45 * k) + " degrees", onZ ? 0.0 : y, onY ? 0.0 : z,
                onZ ? q : 0.0, onZ ? t : 0.0, onY ? q : 0.0, onY ? t : 0.0, torsion);
        }
    }

    const SectionProperties& m_p;
    std::vector<StressPoint> m_points;
};

} // namespace

SectionSolution::SectionSolution(SectionProperties properties, std::vector<StressPoint> points,
                                 std::shared_ptr<const detail::SolidField> field)
    : m_properties(std::move(properties))
    , m_points(std::move(points))
    , m_field(std::move(field))
{}

Expected<std::shared_ptr<const SectionSolution>> solveSection(const Section& section)
{
    auto solved = solve(section);
    if (!solved) {
        return tl::unexpected(solved.error());
    }
    std::vector<StressPoint> points;
    if (section.shape) {
        PointBuilder builder(solved->properties);
        points = std::visit(builder, *section.shape);
    }
    return std::make_shared<const SectionSolution>(solved->properties, std::move(points),
                                                   std::move(solved->field));
}

Expected<SectionProperties> sectionProperties(const Section& section)
{
    const auto solved = solve(section);
    if (!solved) {
        return tl::unexpected(solved.error());
    }
    return solved->properties;
}

std::vector<StressPoint> stressPoints(const SectionShape& shape)
{
    const auto solution = solveSection({.name = "", .shape = shape});
    if (!solution) {
        return {};
    }
    const auto points = (*solution)->points();
    return {points.begin(), points.end()};
}

} // namespace fem::core
