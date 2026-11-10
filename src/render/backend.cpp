#include <fem/render/backend.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace fem::render {

namespace {

constexpr std::string_view kForceFailureName = "fail";

constexpr std::array<std::pair<std::string_view, Backend>, 6> kBackendNames = {{
    {"d3d11", Backend::Direct3D11},
    {"d3d12", Backend::Direct3D12},
    {"metal", Backend::Metal},
    {"vulkan", Backend::Vulkan},
    {"opengl", Backend::OpenGL},
    {"null", Backend::Null},
}};

std::string toLower(std::string_view text)
{
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

std::vector<Backend> defaultBackends(Platform platform)
{
    switch (platform) {
    case Platform::Windows:
        return {Backend::Direct3D11, Backend::OpenGL};
    case Platform::MacOS:
        return {Backend::Metal, Backend::OpenGL};
    case Platform::Linux:
        return {Backend::Vulkan, Backend::OpenGL};
    }
    return {Backend::OpenGL};
}

} // namespace

Platform currentPlatform() noexcept
{
#if defined(_WIN32)
    return Platform::Windows;
#elif defined(__APPLE__)
    return Platform::MacOS;
#else
    return Platform::Linux;
#endif
}

std::optional<Backend> parseBackendName(std::string_view name)
{
    const std::string lower = toLower(name);
    for (const auto& [key, backend] : kBackendNames) {
        if (lower == key) {
            return backend;
        }
    }
    return std::nullopt;
}

std::string_view backendDisplayName(Backend backend) noexcept
{
    switch (backend) {
    case Backend::Direct3D11:
        return "Direct3D 11";
    case Backend::Direct3D12:
        return "Direct3D 12";
    case Backend::Metal:
        return "Metal";
    case Backend::Vulkan:
        return "Vulkan";
    case Backend::OpenGL:
        return "OpenGL";
    case Backend::Null:
        return "Null";
    }
    return "Unknown";
}

bool isBackendSupported(Backend backend, Platform platform) noexcept
{
    switch (backend) {
    case Backend::Direct3D11:
    case Backend::Direct3D12:
        return platform == Platform::Windows;
    case Backend::Metal:
        return platform == Platform::MacOS;
    case Backend::Vulkan:
        return platform == Platform::Windows || platform == Platform::Linux;
    case Backend::OpenGL:
    case Backend::Null:
        return true;
    }
    return false;
}

BackendPlan planBackends(Platform platform, std::string_view overrideName)
{
    BackendPlan plan;
    plan.attempts = defaultBackends(platform);

    if (overrideName.empty()) {
        return plan;
    }
    if (toLower(overrideName) == kForceFailureName) {
        plan.attempts.clear();
        return plan;
    }

    const std::optional<Backend> requested = parseBackendName(overrideName);
    if (!requested) {
        plan.warning = "FEM_RHI_BACKEND: unknown backend '" + std::string(overrideName) +
                       "'; using the platform default";
        return plan;
    }
    if (!isBackendSupported(*requested, platform)) {
        plan.warning = "FEM_RHI_BACKEND: backend '" + std::string(overrideName) +
                       "' is not available on this platform; using the platform default";
        return plan;
    }

    // The requested backend goes first; the defaults stay as fallbacks.
    std::erase(plan.attempts, *requested);
    plan.attempts.insert(plan.attempts.begin(), *requested);
    return plan;
}

} // namespace fem::render
