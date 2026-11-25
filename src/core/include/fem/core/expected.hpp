#pragma once

#include <tl/expected.hpp>

#include <string>
#include <utility>

namespace fem::core {

// An expected failure (invalid file, inconsistent mesh), as opposed to a
// programming error. The message is in English and is a complete sentence
// meant for the user.
struct Error
{
    std::string message;
    bool cancelled = false; // REQ-011-08: stopped by RunControl, not a failure
};

// Value or Error. Exceptions are reserved for programming errors
// (specs/architecture.md, section 3).
template <typename T>
using Expected = tl::expected<T, Error>;

[[nodiscard]] inline tl::unexpected<Error> makeError(std::string message)
{
    return tl::unexpected<Error>(Error{.message = std::move(message), .cancelled = false});
}

// REQ-011-08: the failure an analysis returns when its RunControl is cancelled.
[[nodiscard]] inline tl::unexpected<Error> makeCancelled()
{
    return tl::unexpected<Error>(
        Error{.message = "The analysis was cancelled.", .cancelled = true});
}

} // namespace fem::core
