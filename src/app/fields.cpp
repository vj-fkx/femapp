#include "fields.hpp"

#include <fem/setup/actions.hpp>

#include <QCoreApplication>
#include <QLocale>

#include <array>
#include <cmath>
#include <string>
#include <type_traits>
#include <utility>

namespace fem::app {

namespace {

namespace q = setup::quantity;

// The translation context of the field labels.
class FieldText
{
    Q_DECLARE_TR_FUNCTIONS(FieldText)
};

QString text(const std::string& s)
{
    return QString::fromStdString(s);
}

constexpr setup::Quantity kWarpingSpring{.force = 1, .length = 3};
constexpr setup::Quantity kPerLength{.length = -1};

bool inRange(double v, Range range)
{
    if (!std::isfinite(v)) {
        return false;
    }
    switch (range) {
    case Range::Any:
        return true;
    case Range::Positive:
        return v > 0.0;
    case Range::NonNegative:
        return v >= 0.0;
    case Range::Poisson:
        return v > -1.0 && v < 0.5;
    }
    return false;
}

// The part of an item a field reads and writes: the item itself, or the
// action of a load.
template <typename T>
T& part(setup::Item& item)
{
    if constexpr (std::is_same_v<T, setup::MaterialItem> || std::is_same_v<T, setup::SectionItem> ||
                  std::is_same_v<T, setup::BarAssignment> ||
                  std::is_same_v<T, setup::ConnectionItem> ||
                  std::is_same_v<T, setup::OffsetItem> ||
                  std::is_same_v<T, setup::FoundationItem> ||
                  std::is_same_v<T, setup::WarpingEndItem> ||
                  std::is_same_v<T, setup::SupportItem> || std::is_same_v<T, setup::LoadCase> ||
                  std::is_same_v<T, setup::Combination>) {
        return std::get<T>(item);
    } else {
        return std::get<T>(std::get<setup::LoadItem>(item).action);
    }
}

template <typename T>
const T& part(const setup::Item& item)
{
    return part<T>(const_cast<setup::Item&>(item)); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

// The builders of fields of a part T.
template <typename T>
class Fields
{
public:
    explicit Fields(std::vector<Field>& out)
        : m_out(out)
    {}

    void number(const QString& label, setup::Quantity quantity, Range range,
                const std::function<double&(T&)>& at)
    {
        m_out.push_back({.label = label,
                         .kind = FieldKind::Number,
                         .quantity = quantity,
                         .optional = false,
                         .range = range,
                         .choices = {},
                         .get =
                             [at](const setup::Item& i) {
                                 return FieldValue{at(const_cast<T&>(part<T>(i)))}; // NOLINT
                             },
                         .set =
                             [at, range](setup::Item& i, const FieldValue& v) {
                                 const auto* d = std::get_if<double>(&v);
                                 if (d == nullptr || !inRange(*d, range)) {
                                     return false;
                                 }
                                 at(part<T>(i)) = *d;
                                 return true;
                             }});
    }

    void optional(const QString& label, setup::Quantity quantity, Range range,
                  const std::function<std::optional<double>&(T&)>& at)
    {
        m_out.push_back({.label = label,
                         .kind = FieldKind::Number,
                         .quantity = quantity,
                         .optional = true,
                         .range = range,
                         .choices = {},
                         .get = [at](const setup::Item& i) -> FieldValue {
                             const auto& v = at(const_cast<T&>(part<T>(i))); // NOLINT
                             return v ? FieldValue{*v} : FieldValue{};
                         },
                         .set =
                             [at, range](setup::Item& i, const FieldValue& v) {
                                 if (std::holds_alternative<std::monostate>(v)) {
                                     at(part<T>(i)).reset();
                                     return true;
                                 }
                                 const auto* d = std::get_if<double>(&v);
                                 if (d == nullptr || !inRange(*d, range)) {
                                     return false;
                                 }
                                 at(part<T>(i)) = *d;
                                 return true;
                             }});
    }

    void vector(const QString& label, setup::Quantity quantity,
                const std::function<Eigen::Vector3d&(T&)>& at)
    {
        m_out.push_back({.label = label,
                         .kind = FieldKind::Vector,
                         .quantity = quantity,
                         .optional = false,
                         .range = Range::Any,
                         .choices = {},
                         .get =
                             [at](const setup::Item& i) {
                                 return FieldValue{at(const_cast<T&>(part<T>(i)))}; // NOLINT
                             },
                         .set =
                             [at](setup::Item& i, const FieldValue& v) {
                                 const auto* x = std::get_if<Eigen::Vector3d>(&v);
                                 if (x == nullptr || !x->allFinite()) {
                                     return false;
                                 }
                                 at(part<T>(i)) = *x;
                                 return true;
                             }});
    }

    void flag(const QString& label, const std::function<bool&(T&)>& at)
    {
        m_out.push_back({.label = label,
                         .kind = FieldKind::Flag,
                         .quantity = {},
                         .optional = false,
                         .range = Range::Any,
                         .choices = {},
                         .get =
                             [at](const setup::Item& i) {
                                 return FieldValue{at(const_cast<T&>(part<T>(i)))}; // NOLINT
                             },
                         .set =
                             [at](setup::Item& i, const FieldValue& v) {
                                 const auto* b = std::get_if<bool>(&v);
                                 if (b == nullptr) {
                                     return false;
                                 }
                                 at(part<T>(i)) = *b;
                                 return true;
                             }});
    }

    // A choice among `choices`, read and written as an index.
    void choice(const QString& label, const QStringList& choices,
                const std::function<int(const T&)>& get, const std::function<void(T&, int)>& set)
    {
        const auto count = static_cast<int>(choices.size());
        m_out.push_back({.label = label,
                         .kind = FieldKind::Choice,
                         .quantity = {},
                         .optional = false,
                         .range = Range::Any,
                         .choices = choices,
                         .get = [get](const setup::Item& i) { return FieldValue{get(part<T>(i))}; },
                         .set =
                             [set, count](setup::Item& i, const FieldValue& v) {
                                 const auto* index = std::get_if<int>(&v);
                                 if (index == nullptr || *index < 0 || *index >= count) {
                                     return false;
                                 }
                                 set(part<T>(i), *index);
                                 return true;
                             }});
    }

    // A name: any text but an empty one.
    void name(const QString& label, const std::function<std::string&(T&)>& at)
    {
        m_out.push_back({.label = label,
                         .kind = FieldKind::Text,
                         .quantity = {},
                         .optional = false,
                         .range = Range::Any,
                         .choices = {},
                         .get =
                             [at](const setup::Item& i) {
                                 return FieldValue{text(at(const_cast<T&>(part<T>(i))))}; // NOLINT
                             },
                         .set =
                             [at](setup::Item& i, const FieldValue& v) {
                                 const auto* s = std::get_if<QString>(&v);
                                 if (s == nullptr || s->trimmed().isEmpty()) {
                                     return false;
                                 }
                                 at(part<T>(i)) = s->trimmed().toStdString();
                                 return true;
                             }});
    }

    // Read-only text.
    void info(const QString& label, const std::function<QString(const T&)>& get)
    {
        m_out.push_back({.label = label,
                         .kind = FieldKind::Info,
                         .quantity = {},
                         .optional = false,
                         .range = Range::Any,
                         .choices = {},
                         .get = [get](const setup::Item& i) { return FieldValue{get(part<T>(i))}; },
                         .set = nullptr});
    }

private:
    std::vector<Field>& m_out;
};

// An enum shown as a choice, by its underlying index.
template <typename E>
int indexOf(E value)
{
    return static_cast<int>(value);
}

template <typename E>
E fromIndex(int index)
{
    return static_cast<E>(index);
}

QStringList endChoices()
{
    return {FieldText::tr("Start"), FieldText::tr("End")};
}

int endIndex(core::BarEnd end)
{
    return end == core::BarEnd::End ? 1 : 0;
}

core::BarEnd endOf(int index)
{
    return index == 1 ? core::BarEnd::End : core::BarEnd::Start;
}

QStringList namesOf(const setup::Document& document, bool materials)
{
    QStringList names;
    for (const setup::Item& item : document.items()) {
        if (materials) {
            if (const auto* m = std::get_if<setup::MaterialItem>(&item)) {
                names.append(text(m->data.name));
            }
        } else if (const auto* s = std::get_if<setup::SectionItem>(&item)) {
            names.append(text(s->data.name));
        }
    }
    return names;
}

// A name chosen from `names`; a name not among them (an unknown material)
// is shown first, so that it can be seen and replaced.
template <typename T>
void nameChoice(Fields<T>& f, const QString& label, QStringList names,
                const std::function<std::string&(T&)>& at, const std::string& current)
{
    if (!names.contains(text(current))) {
        names.prepend(text(current));
    }
    f.choice(
        label, names,
        [names, at](const T& t) {
            return static_cast<int>(names.indexOf(text(at(const_cast<T&>(t))))); // NOLINT
        },
        [names, at](T& t, int index) { at(t) = names.at(index).toStdString(); });
}

void materialFields(std::vector<Field>& out, const setup::MaterialItem& item)
{
    Fields<setup::MaterialItem> f(out);
    f.name(FieldText::tr("Name"), [](auto& m) -> std::string& { return m.data.name; });
    f.info(FieldText::tr("Standard"), [](const auto& m) {
        return m.standard.empty() ? QString() : text(m.grade + " (" + m.standard + ")");
    });
    f.optional(FieldText::tr("E"), q::kStress, Range::Positive,
               [](auto& m) -> std::optional<double>& { return m.data.youngsModulus; });
    f.optional(FieldText::tr("ν"), q::kNone, Range::Poisson,
               [](auto& m) -> std::optional<double>& { return m.data.poissonsRatio; });
    f.optional(FieldText::tr("ρ"), q::kDensity, Range::NonNegative,
               [](auto& m) -> std::optional<double>& { return m.data.density; });
    f.optional(FieldText::tr("α"), q::kPerTemperature, Range::Any,
               [](auto& m) -> std::optional<double>& { return m.data.thermalExpansion; });
    f.optional(FieldText::tr("k"), q::kConductivity, Range::NonNegative,
               [](auto& m) -> std::optional<double>& { return m.data.conductivity; });
    f.optional(FieldText::tr("c"), q::kSpecificHeat, Range::NonNegative,
               [](auto& m) -> std::optional<double>& { return m.data.specificHeat; });
    // The extra properties, each with its own quantity (design §5.9).
    for (std::size_t k = 0; k < item.properties.size(); ++k) {
        f.number(text(item.properties[k].key), item.properties[k].quantity, Range::Any,
                 [k](auto& m) -> double& { return m.properties.at(k).value; });
    }
}

void sectionFields(std::vector<Field>& out, const setup::SectionItem& item)
{
    Fields<setup::SectionItem> f(out);
    f.name(FieldText::tr("Name"), [](auto& s) -> std::string& { return s.data.name; });
    if (!item.data.shape) {
        const auto generic = [&f](const QString& label, setup::Quantity quantity, Range range,
                                  std::optional<double> core::Section::* member) {
            f.optional(label, quantity, range,
                       [member](auto& s) -> std::optional<double>& { return s.data.*member; });
        };
        generic(FieldText::tr("A"), q::kArea, Range::Positive, &core::Section::area);
        generic(FieldText::tr("Iy"), q::kInertia, Range::Positive, &core::Section::inertiaY);
        generic(FieldText::tr("Iz"), q::kInertia, Range::Positive, &core::Section::inertiaZ);
        generic(FieldText::tr("Iyz"), q::kInertia, Range::Any, &core::Section::productOfInertia);
        generic(FieldText::tr("J"), q::kInertia, Range::NonNegative,
                &core::Section::torsionConstant);
        generic(FieldText::tr("Asy"), q::kArea, Range::Positive, &core::Section::shearAreaY);
        generic(FieldText::tr("Asz"), q::kArea, Range::Positive, &core::Section::shearAreaZ);
        generic(FieldText::tr("Iw"), q::kWarping, Range::NonNegative,
                &core::Section::warpingConstant);
        generic(FieldText::tr("βy"), q::kLength, Range::Any, &core::Section::monosymmetryY);
        generic(FieldText::tr("βz"), q::kLength, Range::Any, &core::Section::monosymmetryZ);
        return;
    }
    // The dimensions of a parametric shape, all positive lengths.
    const auto dimension = [&f](const QString& label, auto getter) {
        f.number(label, q::kLength, Range::Positive, getter);
    };
    std::visit(
        [&](const auto& shape) {
            using S = std::decay_t<decltype(shape)>;
            const auto of = [](auto& s) -> S& { return std::get<S>(*s.data.shape); };
            if constexpr (std::is_same_v<S, core::Rectangle>) {
                dimension(FieldText::tr("b"), [of](auto& s) -> double& { return of(s).b; });
                dimension(FieldText::tr("h"), [of](auto& s) -> double& { return of(s).h; });
            } else if constexpr (std::is_same_v<S, core::Circle>) {
                dimension(FieldText::tr("d"), [of](auto& s) -> double& { return of(s).d; });
            } else if constexpr (std::is_same_v<S, core::CircularTube>) {
                dimension(FieldText::tr("d"), [of](auto& s) -> double& { return of(s).d; });
                dimension(FieldText::tr("t"), [of](auto& s) -> double& { return of(s).t; });
            } else if constexpr (std::is_same_v<S, core::RectangularTube> ||
                                 std::is_same_v<S, core::Angle>) {
                dimension(FieldText::tr("b"), [of](auto& s) -> double& { return of(s).b; });
                dimension(FieldText::tr("h"), [of](auto& s) -> double& { return of(s).h; });
                dimension(FieldText::tr("t"), [of](auto& s) -> double& { return of(s).t; });
            } else if constexpr (std::is_same_v<S, core::ISection> ||
                                 std::is_same_v<S, core::Channel> || std::is_same_v<S, core::Tee>) {
                dimension(FieldText::tr("h"), [of](auto& s) -> double& { return of(s).h; });
                dimension(FieldText::tr("bf"), [of](auto& s) -> double& { return of(s).bf; });
                dimension(FieldText::tr("tf"), [of](auto& s) -> double& { return of(s).tf; });
                dimension(FieldText::tr("tw"), [of](auto& s) -> double& { return of(s).tw; });
            } else {
                // Thin-walled and solid polygons have their own editors.
                f.info(FieldText::tr("Shape"),
                       [](const auto&) { return FieldText::tr("Edited in its table"); });
            }
        },
        *item.data.shape);
}

void barFields(std::vector<Field>& out, const setup::BarAssignment& item,
               const setup::Document& document)
{
    Fields<setup::BarAssignment> f(out);
    f.choice(
        FieldText::tr("Kind"),
        {FieldText::tr("Truss 2D"), FieldText::tr("Truss 3D"), FieldText::tr("Beam 2D"),
         FieldText::tr("Beam 3D")},
        [](const auto& b) { return indexOf(b.data.kind); },
        [](auto& b, int i) { b.data.kind = fromIndex<core::ElementKind>(i); });
    nameChoice<setup::BarAssignment>(
        f, FieldText::tr("Material"), namesOf(document, true),
        [](auto& b) -> std::string& { return b.data.material; }, item.data.material);
    nameChoice<setup::BarAssignment>(
        f, FieldText::tr("Section"), namesOf(document, false),
        [](auto& b) -> std::string& { return b.data.section; }, item.data.section);
    f.choice(
        FieldText::tr("Theory"), {FieldText::tr("Euler-Bernoulli"), FieldText::tr("Timoshenko")},
        [](const auto& b) { return indexOf(b.data.theory); },
        [](auto& b, int i) { b.data.theory = fromIndex<core::BeamTheory>(i); });
    f.number(FieldText::tr("Roll angle (rad)"), q::kNone, Range::Any,
             [](auto& b) -> double& { return b.data.rollAngle; });
    f.flag(FieldText::tr("Warping"), [](auto& b) -> bool& { return b.data.warping; });
    f.choice(
        FieldText::tr("Curved beams"), {FieldText::tr("Arc"), FieldText::tr("Isoparametric")},
        [](const auto& b) { return indexOf(b.data.curved); },
        [](auto& b, int i) { b.data.curved = fromIndex<core::CurvedFormulation>(i); });
    // REQ-010-10: the limit of the truss element of the 008c.
    f.info(FieldText::tr("Warning"), [](const auto& b) {
        const bool truss =
            b.data.kind == core::ElementKind::Truss2D || b.data.kind == core::ElementKind::Truss3D;
        return truss ? FieldText::tr("A truss bar does not buckle between its nodes in the "
                                     "second order or the buckling analysis: model it as a "
                                     "beam for that.")
                     : QString();
    });
}

void detailFields(std::vector<Field>& out, const setup::Item& item)
{
    if (std::holds_alternative<setup::ConnectionItem>(item)) {
        Fields<setup::ConnectionItem> f(out);
        f.choice(
            FieldText::tr("End"), endChoices(), [](const auto& c) { return endIndex(c.data.end); },
            [](auto& c, int i) { c.data.end = endOf(i); });
        // Empty: rigid; 0: released; a positive value: a spring.
        static const std::array<const char*, 6> kForces{"N", "Vy", "Vz", "T", "My", "Mz"};
        for (std::size_t k = 0; k < kForces.size(); ++k) {
            f.optional(QString::fromLatin1(kForces.at(k)),
                       k < 3 ? q::kForcePerLength : q::kRotationalSpring, Range::NonNegative,
                       [k](auto& c) -> std::optional<double>& { return c.data.stiffness.at(k); });
        }
    } else if (std::holds_alternative<setup::OffsetItem>(item)) {
        Fields<setup::OffsetItem> f(out);
        f.vector(FieldText::tr("Start"), q::kLength,
                 [](auto& o) -> Eigen::Vector3d& { return o.data.start; });
        f.vector(FieldText::tr("End"), q::kLength,
                 [](auto& o) -> Eigen::Vector3d& { return o.data.end; });
    } else if (std::holds_alternative<setup::FoundationItem>(item)) {
        Fields<setup::FoundationItem> f(out);
        f.vector(FieldText::tr("kx, ky, kz"), q::kSpringPerLength,
                 [](auto& o) -> Eigen::Vector3d& { return o.data.translational; });
        f.number(FieldText::tr("Torsional"), q::kForce, Range::NonNegative,
                 [](auto& o) -> double& { return o.data.torsional; });
    } else if (std::holds_alternative<setup::WarpingEndItem>(item)) {
        Fields<setup::WarpingEndItem> f(out);
        f.choice(
            FieldText::tr("End"), endChoices(), [](const auto& w) { return endIndex(w.data.end); },
            [](auto& w, int i) { w.data.end = endOf(i); });
        f.choice(
            FieldText::tr("Condition"),
            {FieldText::tr("Free"), FieldText::tr("Restrained"), FieldText::tr("Spring"),
             FieldText::tr("Continuous")},
            [](const auto& w) { return indexOf(w.data.condition); },
            [](auto& w, int i) { w.data.condition = fromIndex<core::WarpingCondition>(i); });
        f.number(FieldText::tr("Spring"), kWarpingSpring, Range::NonNegative,
                 [](auto& w) -> double& { return w.data.stiffness; });
    }
}

void supportFields(std::vector<Field>& out)
{
    Fields<setup::SupportItem> f(out);
    static const std::array<std::pair<const char*, core::DofType>, 7> kDofs{
        {{"U", core::DofType::U},
         {"V", core::DofType::V},
         {"W", core::DofType::W},
         {"RX", core::DofType::RX},
         {"RY", core::DofType::RY},
         {"RZ", core::DofType::RZ},
         {"Warp", core::DofType::Warp}}};
    for (const auto& [name, dof] : kDofs) {
        const auto d = static_cast<std::size_t>(dof);
        f.flag(QString::fromLatin1(name), [d](auto& s) -> bool& { return s.held.at(d); });
    }
}

void loadCaseFields(std::vector<Field>& out, const setup::LoadCase& item)
{
    Fields<setup::LoadCase> f(out);
    f.name(FieldText::tr("Name"), [](auto& c) -> std::string& { return c.name; });
    // REQ-010-26: the standard and the category of its action.
    QStringList standards{FieldText::tr("(none)")};
    for (const setup::ActionCategory& c : setup::actionCategories()) {
        if (!standards.contains(text(c.standard))) {
            standards.append(text(c.standard));
        }
    }
    f.choice(
        FieldText::tr("Standard"), standards,
        [standards](const auto& c) {
            return c.standard.empty() ? 0 : static_cast<int>(standards.indexOf(text(c.standard)));
        },
        [standards](auto& c, int i) {
            const std::string chosen = i == 0 ? std::string() : standards.at(i).toStdString();
            if (chosen != c.standard) {
                c.standard = chosen;
                c.category.clear();
            }
        });
    if (item.standard.empty()) {
        return;
    }
    QStringList codes{FieldText::tr("(none)")};
    QStringList labels{FieldText::tr("(none)")};
    for (const setup::ActionCategory& c : setup::actionCategories()) {
        if (c.standard == item.standard) {
            codes.append(text(c.code));
            labels.append(text(c.code + " - " + c.name));
        }
    }
    f.choice(
        FieldText::tr("Category"), labels,
        [codes](const auto& c) {
            const auto at = codes.indexOf(text(c.category));
            return at < 0 ? 0 : static_cast<int>(at);
        },
        [codes](auto& c, int i) {
            c.category = i == 0 ? std::string() : codes.at(i).toStdString();
        });
    f.info(FieldText::tr("ψ0, ψ1, ψ2"), [](const auto& c) {
        const setup::ActionCategory* category = setup::findCategory(c.standard, c.category);
        if (category == nullptr || !category->psi0) {
            return QString();
        }
        return QStringLiteral("%1, %2, %3")
            .arg(formatNumber(*category->psi0), formatNumber(category->psi1.value_or(0.0)),
                 formatNumber(category->psi2.value_or(0.0)));
    });
}

void loadFields(std::vector<Field>& out, const setup::LoadItem& item)
{
    std::visit(
        [&](const auto& action) {
            using A = std::decay_t<decltype(action)>;
            Fields<A> f(out);
            if constexpr (std::is_same_v<A, core::NodalForce>) {
                f.vector(FieldText::tr("Force"), q::kForce,
                         [](auto& a) -> Eigen::Vector3d& { return a.force; });
                f.vector(FieldText::tr("Moment"), q::kMoment,
                         [](auto& a) -> Eigen::Vector3d& { return a.moment; });
                f.number(FieldText::tr("Bimoment"), q::kBimoment, Range::Any,
                         [](auto& a) -> double& { return a.bimoment; });
            } else if constexpr (std::is_same_v<A, core::DistributedLoad> ||
                                 std::is_same_v<A, core::ConcentratedLoad>) {
                f.choice(
                    FieldText::tr("Axes"), {FieldText::tr("Global"), FieldText::tr("Local")},
                    [](const auto& a) { return indexOf(a.axes); },
                    [](auto& a, int i) { a.axes = fromIndex<core::Axes>(i); });
                if constexpr (std::is_same_v<A, core::DistributedLoad>) {
                    f.vector(FieldText::tr("Start value"), q::kForcePerLength,
                             [](auto& a) -> Eigen::Vector3d& { return a.startValue; });
                    f.vector(FieldText::tr("End value"), q::kForcePerLength,
                             [](auto& a) -> Eigen::Vector3d& { return a.endValue; });
                    f.flag(FieldText::tr("From as a fraction"),
                           [](auto& a) -> bool& { return a.from.fraction; });
                    f.number(action.from.fraction ? FieldText::tr("From")
                                                  : FieldText::tr("From (length)"),
                             action.from.fraction ? q::kNone : q::kLength, Range::NonNegative,
                             [](auto& a) -> double& { return a.from.value; });
                    f.flag(FieldText::tr("To as a fraction"),
                           [](auto& a) -> bool& { return a.to.fraction; });
                    f.number(action.to.fraction ? FieldText::tr("To")
                                                : FieldText::tr("To (length)"),
                             action.to.fraction ? q::kNone : q::kLength, Range::NonNegative,
                             [](auto& a) -> double& { return a.to.value; });
                    f.number(FieldText::tr("Start torque"), q::kForce, Range::Any,
                             [](auto& a) -> double& { return a.startTorque; });
                    f.number(FieldText::tr("End torque"), q::kForce, Range::Any,
                             [](auto& a) -> double& { return a.endTorque; });
                } else {
                    f.vector(FieldText::tr("Force"), q::kForce,
                             [](auto& a) -> Eigen::Vector3d& { return a.force; });
                    f.vector(FieldText::tr("Moment"), q::kMoment,
                             [](auto& a) -> Eigen::Vector3d& { return a.moment; });
                    f.flag(FieldText::tr("At as a fraction"),
                           [](auto& a) -> bool& { return a.at.fraction; });
                    f.number(action.at.fraction ? FieldText::tr("At")
                                                : FieldText::tr("At (length)"),
                             action.at.fraction ? q::kNone : q::kLength, Range::NonNegative,
                             [](auto& a) -> double& { return a.at.value; });
                }
            } else if constexpr (std::is_same_v<A, setup::SelfWeight>) {
                f.vector(FieldText::tr("Acceleration"), q::kAcceleration,
                         [](auto& a) -> Eigen::Vector3d& { return a.acceleration; });
            } else if constexpr (std::is_same_v<A, core::TemperatureChange>) {
                f.number(FieldText::tr("ΔT"), q::kTemperature, Range::Any,
                         [](auto& a) -> double& { return a.deltaT; });
            } else if constexpr (std::is_same_v<A, core::TemperatureGradient>) {
                f.number(FieldText::tr("dT/dy"), q::kGradient, Range::Any,
                         [](auto& a) -> double& { return a.alongY; });
                f.number(FieldText::tr("dT/dz"), q::kGradient, Range::Any,
                         [](auto& a) -> double& { return a.alongZ; });
            } else if constexpr (std::is_same_v<A, core::InitialStrain>) {
                f.number(FieldText::tr("Strain"), q::kNone, Range::Any,
                         [](auto& a) -> double& { return a.strain; });
            } else if constexpr (std::is_same_v<A, setup::Settlement>) {
                static const QStringList kDofs{QStringLiteral("U"),  QStringLiteral("V"),
                                               QStringLiteral("W"),  QStringLiteral("RX"),
                                               QStringLiteral("RY"), QStringLiteral("RZ"),
                                               QStringLiteral("T"),  QStringLiteral("Warp")};
                f.choice(
                    FieldText::tr("Dof"), kDofs, [](const auto& a) { return indexOf(a.dof); },
                    [](auto& a, int i) { a.dof = fromIndex<core::DofType>(i); });
                setup::Quantity quantity = q::kNone;
                switch (action.dof) {
                case core::DofType::U:
                case core::DofType::V:
                case core::DofType::W:
                    quantity = q::kLength;
                    break;
                case core::DofType::T:
                    quantity = q::kTemperature;
                    break;
                case core::DofType::Warp:
                    quantity = kPerLength;
                    break;
                default:
                    break; // radians
                }
                f.number(FieldText::tr("Value"), quantity, Range::Any,
                         [](auto& a) -> double& { return a.value; });
            }
        },
        item.action);
}

} // namespace

std::vector<Field> fieldsOf(const setup::Item& item, const setup::Document& document)
{
    std::vector<Field> out;
    std::visit(
        [&](const auto& i) {
            using T = std::decay_t<decltype(i)>;
            if constexpr (std::is_same_v<T, setup::MaterialItem>) {
                materialFields(out, i);
            } else if constexpr (std::is_same_v<T, setup::SectionItem>) {
                sectionFields(out, i);
            } else if constexpr (std::is_same_v<T, setup::BarAssignment>) {
                barFields(out, i, document);
            } else if constexpr (std::is_same_v<T, setup::SupportItem>) {
                supportFields(out);
            } else if constexpr (std::is_same_v<T, setup::LoadCase>) {
                loadCaseFields(out, i);
            } else if constexpr (std::is_same_v<T, setup::LoadItem>) {
                loadFields(out, i);
            } else if constexpr (std::is_same_v<T, setup::Combination>) {
                Fields<setup::Combination> f(out);
                f.name(FieldText::tr("Name"), [](auto& c) -> std::string& { return c.name; });
            } else {
                detailFields(out, item);
            }
        },
        item);
    return out;
}

std::optional<FieldValue> parseNumber(const QString& input)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return FieldValue{};
    }
    bool ok = false;
    const double value = QLocale::c().toDouble(trimmed, &ok);
    if (!ok || !std::isfinite(value)) {
        return std::nullopt;
    }
    return FieldValue{value};
}

QString formatNumber(double value)
{
    return QLocale::c().toString(value, 'g', 12);
}

} // namespace fem::app
