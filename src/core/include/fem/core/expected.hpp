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
};

// Value or Error. Exceptions are reserved for programming errors
// (specs/architecture.md, section 3).
template <typename T>
using Expected = tl::expected<T, Error>;

[[nodiscard]] inline tl::unexpected<Error> makeError(std::string message)
{
    return tl::unexpected<Error>(Error{.message = std::move(message)});
}

} // namespace fem::core
