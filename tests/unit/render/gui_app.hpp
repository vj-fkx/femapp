#pragma once

// QPainter text (used for the axis labels) and the renderer need a
// QGuiApplication. One for the whole test binary (an inline function has
// one static), on the offscreen platform so that no display is needed.

#include <QByteArray>
#include <QGuiApplication>

#include <array>
#include <memory>

namespace fem::render::test {

inline void ensureGuiApplication()
{
    static int argc = 1;
    static std::array<char, 12> name = {"test_render"};
    static std::array<char*, 2> argv = {name.data(), nullptr};
    static const std::unique_ptr<QGuiApplication> app = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        return std::make_unique<QGuiApplication>(argc, argv.data());
    }();
}

} // namespace fem::render::test
