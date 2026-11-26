#pragma once

#include <QCoreApplication>

#include <array>
#include <string>

namespace fem::app::test {

// One application for the whole test executable, for the tests that need
// an event loop.
inline QCoreApplication& application()
{
    static int argc = 1;
    static std::string name = "test_app";
    static std::array<char*, 2> argv{name.data(), nullptr};
    static QCoreApplication app(argc, argv.data());
    return app;
}

} // namespace fem::app::test
