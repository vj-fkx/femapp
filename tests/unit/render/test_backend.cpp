#include <fem/render/backend.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using fem::render::Backend;
using fem::render::BackendPlan;
using fem::render::planBackends;
using fem::render::Platform;

// REQ-002-04
TEST_CASE("backend names are parsed case-insensitively", "[render][backend]")
{
    using fem::render::parseBackendName;

    CHECK(parseBackendName("d3d11") == Backend::Direct3D11);
    CHECK(parseBackendName("d3d12") == Backend::Direct3D12);
    CHECK(parseBackendName("metal") == Backend::Metal);
    CHECK(parseBackendName("vulkan") == Backend::Vulkan);
    CHECK(parseBackendName("opengl") == Backend::OpenGL);
    CHECK(parseBackendName("null") == Backend::Null);
    CHECK(parseBackendName("OpenGL") == Backend::OpenGL);

    CHECK_FALSE(parseBackendName("").has_value());
    CHECK_FALSE(parseBackendName("directx").has_value());
    CHECK_FALSE(parseBackendName("fail").has_value());
}

// REQ-002-02, REQ-002-03
TEST_CASE("default plan is the native backend followed by OpenGL", "[render][backend]")
{
    CHECK(planBackends(Platform::Windows, "").attempts ==
          std::vector{Backend::Direct3D11, Backend::OpenGL});
    CHECK(planBackends(Platform::MacOS, "").attempts ==
          std::vector{Backend::Metal, Backend::OpenGL});
    CHECK(planBackends(Platform::Linux, "").attempts ==
          std::vector{Backend::Vulkan, Backend::OpenGL});
    CHECK(planBackends(Platform::Linux, "").warning.empty());
}

// REQ-002-04
TEST_CASE("a valid override is tried first and keeps the fallbacks", "[render][backend]")
{
    const BackendPlan d3d12 = planBackends(Platform::Windows, "d3d12");
    CHECK(d3d12.attempts == std::vector{Backend::Direct3D12, Backend::Direct3D11, Backend::OpenGL});
    CHECK(d3d12.warning.empty());

    const BackendPlan opengl = planBackends(Platform::Windows, "opengl");
    CHECK(opengl.attempts == std::vector{Backend::OpenGL, Backend::Direct3D11});
    CHECK(opengl.warning.empty());

    for (const Platform platform : {Platform::Windows, Platform::MacOS, Platform::Linux}) {
        const BackendPlan null = planBackends(platform, "null");
        REQUIRE_FALSE(null.attempts.empty());
        CHECK(null.attempts.front() == Backend::Null);
        CHECK(null.warning.empty());
    }
}

// REQ-002-05
TEST_CASE("an unknown or unsupported override falls back with a warning", "[render][backend]")
{
    const BackendPlan unknown = planBackends(Platform::Linux, "directx");
    CHECK(unknown.attempts == planBackends(Platform::Linux, "").attempts);
    CHECK_FALSE(unknown.warning.empty());

    const BackendPlan unsupported = planBackends(Platform::Windows, "metal");
    CHECK(unsupported.attempts == planBackends(Platform::Windows, "").attempts);
    CHECK_FALSE(unsupported.warning.empty());

    CHECK_FALSE(planBackends(Platform::MacOS, "d3d11").warning.empty());
    CHECK_FALSE(planBackends(Platform::MacOS, "vulkan").warning.empty());
}

// REQ-002-06: test hook for the "no backend available" path.
TEST_CASE("the internal 'fail' override yields an empty plan", "[render][backend]")
{
    const BackendPlan plan = planBackends(Platform::Windows, "fail");
    CHECK(plan.attempts.empty());
    CHECK(plan.warning.empty());
}

TEST_CASE("backend support and display names", "[render][backend]")
{
    using fem::render::backendDisplayName;
    using fem::render::isBackendSupported;

    CHECK(isBackendSupported(Backend::Direct3D11, Platform::Windows));
    CHECK_FALSE(isBackendSupported(Backend::Direct3D11, Platform::Linux));
    CHECK(isBackendSupported(Backend::Metal, Platform::MacOS));
    CHECK_FALSE(isBackendSupported(Backend::Metal, Platform::Windows));
    CHECK(isBackendSupported(Backend::Vulkan, Platform::Linux));
    CHECK(isBackendSupported(Backend::OpenGL, Platform::MacOS));
    CHECK(isBackendSupported(Backend::Null, Platform::Linux));

    CHECK(backendDisplayName(Backend::Direct3D11) == "Direct3D 11");
    CHECK(backendDisplayName(Backend::OpenGL) == "OpenGL");
}
