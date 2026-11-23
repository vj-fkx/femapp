#include <fem/setup/convert.hpp>

#include <cstddef>
#include <optional>
#include <type_traits>
#include <variant>

namespace fem::setup {

namespace {

namespace q = quantity;

// The quantities that only appear here (design §5.1).
constexpr Quantity kWarpingSpring{.force = 1, .length = 3}; // B = kw * psi, psi in 1/L
constexpr Quantity kPerLength{.length = -1};                // the warping dof psi

class Converter
{
public:
    Converter(UnitSystem from, UnitSystem to)
        : m_from(from)
        , m_to(to)
    {}

    [[nodiscard]] double factor(Quantity quantity) const
    {
        return conversionFactor(m_from, m_to, quantity);
    }

    void operator()(double& value, Quantity quantity) const { value *= factor(quantity); }

    void operator()(std::optional<double>& value, Quantity quantity) const
    {
        if (value) {
            *value *= factor(quantity);
        }
    }

    template <int N>
    void operator()(Eigen::Matrix<double, N, 1>& value, Quantity quantity) const
    {
        value *= factor(quantity);
    }

    template <int N>
    void operator()(std::optional<Eigen::Matrix<double, N, 1>>& value, Quantity quantity) const
    {
        if (value) {
            *value *= factor(quantity);
        }
    }

    // A position along a bar: a length unless it is a fraction.
    void operator()(core::BarPosition& position) const
    {
        if (!position.fraction) {
            position.value *= factor(q::kLength);
        }
    }

private:
    UnitSystem m_from;
    UnitSystem m_to;
};

void convertShape(core::SectionShape& shape, const Converter& c)
{
    std::visit(
        [&](auto& sh) {
            using T = std::decay_t<decltype(sh)>;
            if constexpr (std::is_same_v<T, core::Rectangle>) {
                c(sh.b, q::kLength);
                c(sh.h, q::kLength);
            } else if constexpr (std::is_same_v<T, core::Circle>) {
                c(sh.d, q::kLength);
            } else if constexpr (std::is_same_v<T, core::CircularTube>) {
                c(sh.d, q::kLength);
                c(sh.t, q::kLength);
            } else if constexpr (std::is_same_v<T, core::RectangularTube> ||
                                 std::is_same_v<T, core::Angle>) {
                c(sh.b, q::kLength);
                c(sh.h, q::kLength);
                c(sh.t, q::kLength);
            } else if constexpr (std::is_same_v<T, core::ISection> ||
                                 std::is_same_v<T, core::Channel> || std::is_same_v<T, core::Tee>) {
                c(sh.h, q::kLength);
                c(sh.bf, q::kLength);
                c(sh.tf, q::kLength);
                c(sh.tw, q::kLength);
            } else if constexpr (std::is_same_v<T, core::ThinWalled>) {
                for (Eigen::Vector2d& p : sh.points) {
                    c(p, q::kLength);
                }
                for (core::ThinWalled::Wall& wall : sh.walls) {
                    c(wall.t, q::kLength);
                }
            } else if constexpr (std::is_same_v<T, core::SolidPolygon>) {
                for (core::SolidPolygon::Region& region : sh.regions) {
                    for (Eigen::Vector2d& p : region.outline) {
                        c(p, q::kLength);
                    }
                    for (auto& hole : region.holes) {
                        for (Eigen::Vector2d& p : hole) {
                            c(p, q::kLength);
                        }
                    }
                }
                c(sh.meshSize, q::kLength);
            }
        },
        shape);
}

void convertSection(core::Section& s, const Converter& c)
{
    c(s.area, q::kArea);
    c(s.inertiaY, q::kInertia);
    c(s.inertiaZ, q::kInertia);
    c(s.productOfInertia, q::kInertia);
    c(s.torsionConstant, q::kInertia);
    c(s.shearCentre, q::kLength);
    c(s.shearAreaY, q::kArea);
    c(s.shearAreaZ, q::kArea);
    c(s.warpingConstant, q::kWarping);
    // REQ-008d-12: beta_y and beta_z are lengths.
    c(s.monosymmetryY, q::kLength);
    c(s.monosymmetryZ, q::kLength);
    if (s.shape) {
        convertShape(*s.shape, c);
    }
}

void convertAction(Action& action, const Converter& c)
{
    std::visit(
        [&](auto& a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, core::NodalForce>) {
                c(a.force, q::kForce);
                c(a.moment, q::kMoment);
                c(a.bimoment, q::kBimoment);
            } else if constexpr (std::is_same_v<T, core::DistributedLoad>) {
                c(a.startValue, q::kForcePerLength);
                c(a.endValue, q::kForcePerLength);
                c(a.from);
                c(a.to);
                // A torque per length: F L / L.
                c(a.startTorque, q::kForce);
                c(a.endTorque, q::kForce);
                c(a.point, q::kLength);
            } else if constexpr (std::is_same_v<T, core::ConcentratedLoad>) {
                c(a.force, q::kForce);
                c(a.moment, q::kMoment);
                c(a.at);
                c(a.point, q::kLength);
            } else if constexpr (std::is_same_v<T, SelfWeight>) {
                c(a.acceleration, q::kAcceleration);
            } else if constexpr (std::is_same_v<T, core::TemperatureChange>) {
                c(a.deltaT, q::kTemperature);
            } else if constexpr (std::is_same_v<T, core::TemperatureGradient>) {
                c(a.alongY, q::kGradient);
                c(a.alongZ, q::kGradient);
            } else if constexpr (std::is_same_v<T, core::InitialStrain>) {
                // A strain has no unit.
            } else if constexpr (std::is_same_v<T, Settlement>) {
                switch (a.dof) {
                case core::DofType::U:
                case core::DofType::V:
                case core::DofType::W:
                    c(a.value, q::kLength);
                    break;
                case core::DofType::RX:
                case core::DofType::RY:
                case core::DofType::RZ:
                    break; // radians
                case core::DofType::T:
                    c(a.value, q::kTemperature);
                    break;
                case core::DofType::Warp:
                    c(a.value, kPerLength);
                    break;
                }
            }
        },
        action);
}

} // namespace

void convertItem(Item& item, UnitSystem from, UnitSystem to)
{
    if (from == to) {
        return;
    }
    const Converter c(from, to);
    std::visit(
        [&](auto& i) {
            using T = std::decay_t<decltype(i)>;
            if constexpr (std::is_same_v<T, MaterialItem>) {
                c(i.data.youngsModulus, q::kStress);
                // nu has no unit.
                c(i.data.density, q::kDensity);
                c(i.data.thermalExpansion, q::kPerTemperature);
                c(i.data.conductivity, q::kConductivity);
                c(i.data.specificHeat, q::kSpecificHeat);
                // The extra properties by their own quantity (design §5.9).
                for (MaterialProperty& p : i.properties) {
                    c(p.value, p.quantity);
                }
            } else if constexpr (std::is_same_v<T, SectionItem>) {
                convertSection(i.data, c);
            } else if constexpr (std::is_same_v<T, ConnectionItem>) {
                // N, Vy, Vz: force per length; T, My, Mz: moment per radian.
                for (std::size_t k = 0; k < i.data.stiffness.size(); ++k) {
                    c(i.data.stiffness.at(k), k < 3 ? q::kForcePerLength : q::kRotationalSpring);
                }
            } else if constexpr (std::is_same_v<T, OffsetItem>) {
                c(i.data.start, q::kLength);
                c(i.data.end, q::kLength);
            } else if constexpr (std::is_same_v<T, FoundationItem>) {
                // Winkler per length: F / L / L; torsional: F L / rad / L.
                c(i.data.translational, q::kSpringPerLength);
                c(i.data.torsional, q::kForce);
            } else if constexpr (std::is_same_v<T, WarpingEndItem>) {
                c(i.data.stiffness, kWarpingSpring);
            } else if constexpr (std::is_same_v<T, LoadItem>) {
                convertAction(i.action, c);
            }
            // BarAssignment (a direction and an angle), SupportItem, LoadCase
            // and Combination have nothing with a unit.
        },
        item);
}

} // namespace fem::setup
