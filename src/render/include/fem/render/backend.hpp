#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fem::render {

// Graphics backends reachable through Qt RHI.
enum class Backend { Direct3D11, Direct3D12, Metal, Vulkan, OpenGL, Null };

enum class Platform { Windows, MacOS, Linux };

// Platform this binary was built for.
[[nodiscard]] Platform currentPlatform() noexcept;

// Parses a FEM_RHI_BACKEND value (case-insensitive): d3d11, d3d12, metal,
// vulkan, opengl or null.
[[nodiscard]] std::optional<Backend> parseBackendName(std::string_view name);

// Human-readable name, e.g. "Direct3D 11".
[[nodiscard]] std::string_view backendDisplayName(Backend backend) noexcept;

[[nodiscard]] bool isBackendSupported(Backend backend, Platform platform) noexcept;

struct BackendPlan
{
    // Backends to try, in order. Empty means "no backend is available".
    std::vector<Backend> attempts;
    // Non-empty when the override was rejected and the defaults are used.
    std::string warning;
};

// Builds the list of backends to try on `platform`. `overrideName` is the
// value of the FEM_RHI_BACKEND environment variable (empty when unset).
//
// The internal value "fail" yields an empty plan; it exists so that tests can
// exercise the "no backend could be initialized" path.
[[nodiscard]] BackendPlan planBackends(Platform platform, std::string_view overrideName);

} // namespace fem::render
