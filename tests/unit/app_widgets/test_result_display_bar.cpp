// Feature 012 (REQ-012-01, 06, 07, 09, 11, 12, 15, 16, 17): the options of
// the display of a result in the Results dock, on the offscreen platform.

#include "result_display_bar.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QToolButton>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {

using fem::app::ResultDisplayBar;
using fem::app::ResultDisplaySettings;
using fem::core::ResultQuantity;

void ensureApplication()
{
    static int argc = 1;
    static std::array<char, 18> name = {"test_app_widgets"};
    static std::array<char*, 2> argv = {name.data(), nullptr};
    static const std::unique_ptr<QApplication> app = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        return std::make_unique<QApplication>(argc, argv.data());
    }();
}

template <typename T>
T* child(const ResultDisplayBar& bar, const char* name)
{
    auto* widget = bar.findChild<T*>(QString::fromLatin1(name));
    if (widget == nullptr) {
        throw std::logic_error(name);
    }
    return widget;
}

} // namespace

// The controls show the settings given, and their edits change them.
TEST_CASE("the options bar of the result display", "[app][widgets][result]")
{
    ensureApplication();
    ResultDisplayBar bar;
    std::optional<ResultDisplaySettings> emitted;
    QObject::connect(&bar, &ResultDisplayBar::settingsChanged,
                     [&emitted](const ResultDisplaySettings& s) { emitted = s; });

    ResultDisplaySettings settings;
    settings.quantity = ResultQuantity::Mz;
    settings.scaleMode = fem::app::ScaleMode::User;
    settings.userScale = 40.0;
    settings.bands = 7;
    settings.original = true;
    settings.reactions = false;
    bar.setSettings(settings);
    CHECK_FALSE(emitted.has_value()); // no signal for settings given
    CHECK(child<QComboBox>(bar, "quantity")->currentText() == QStringLiteral("Mz"));
    CHECK(child<QDoubleSpinBox>(bar, "userScale")->value() == 40.0);
    CHECK(child<QDoubleSpinBox>(bar, "userScale")->isEnabled());
    CHECK(child<QSpinBox>(bar, "bands")->value() == 7);
    CHECK(child<QCheckBox>(bar, "original")->isChecked());
    CHECK_FALSE(child<QCheckBox>(bar, "reactions")->isChecked());
    CHECK_FALSE(child<QDoubleSpinBox>(bar, "rangeMin")->isEnabled());

    // REQ-012-12: the bands.
    child<QSpinBox>(bar, "bands")->setValue(3);
    REQUIRE(emitted.has_value());
    CHECK(emitted.value_or(ResultDisplaySettings{}).bands == 3);
    CHECK(bar.settings().bands == 3);

    // REQ-012-09: "None" turns the colors off; a quantity turns them on.
    auto* quantity = child<QComboBox>(bar, "quantity");
    quantity->setCurrentIndex(0);
    CHECK_FALSE(bar.settings().colored);
    quantity->setCurrentIndex(quantity->findText(QStringLiteral("Vy")));
    CHECK(bar.settings().colored);
    CHECK(bar.settings().quantity == ResultQuantity::Vy);

    // REQ-012-11: a fixed range; min >= max is refused.
    child<QCheckBox>(bar, "fixedRange")->setChecked(true);
    CHECK(bar.settings().rangeFixed);
    auto* min = child<QDoubleSpinBox>(bar, "rangeMin");
    auto* max = child<QDoubleSpinBox>(bar, "rangeMax");
    min->setValue(-5.0);
    max->setValue(10.0);
    emit max->editingFinished();
    CHECK(bar.settings().rangeMin == -5.0);
    CHECK(bar.settings().rangeMax == 10.0);
    min->setValue(20.0);
    emit min->editingFinished();
    CHECK(bar.settings().rangeMin == -5.0);
    CHECK(min->value() == -5.0);

    // REQ-012-15: play and pause.
    auto* play = child<QToolButton>(bar, "play");
    play->setChecked(true);
    CHECK(bar.settings().playing);
    CHECK(play->text() == QStringLiteral("Pause"));

    // REQ-012-06: the automatic factor needs no value.
    auto* mode = child<QComboBox>(bar, "scaleMode");
    mode->setCurrentIndex(mode->findText(QStringLiteral("Auto")));
    CHECK(bar.settings().scaleMode == fem::app::ScaleMode::Automatic);
    CHECK_FALSE(child<QDoubleSpinBox>(bar, "userScale")->isEnabled());
}

// REQ-012-01, 09: shown or not, available or not, and the quantities of a
// buckling mode.
TEST_CASE("the options bar follows the result chosen", "[app][widgets][result]")
{
    ensureApplication();
    ResultDisplayBar bar;
    auto* shown = child<QCheckBox>(bar, "shown");
    CHECK_FALSE(shown->isEnabled());
    bar.setAvailable(true);
    CHECK(shown->isEnabled());
    std::optional<bool> requested;
    QObject::connect(&bar, &ResultDisplayBar::shownRequested,
                     [&requested](bool on) { requested = on; });
    shown->setChecked(true);
    CHECK(requested == std::optional<bool>(true));
    bar.setShownState(false);
    CHECK_FALSE(shown->isChecked());

    ResultDisplaySettings settings;
    settings.quantity = ResultQuantity::Mz;
    bar.setSettings(settings);
    // A buckling mode offers no forces: |u| shows, the choice is kept.
    bar.setQuantities({ResultQuantity::Ux, ResultQuantity::UMagnitude, ResultQuantity::Rz});
    auto* quantity = child<QComboBox>(bar, "quantity");
    CHECK(quantity->count() == 4); // with "None"
    CHECK(quantity->currentText() == QStringLiteral("|u|"));
    CHECK(bar.settings().quantity == ResultQuantity::Mz);
}
