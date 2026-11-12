#pragma once

#include <fem/core/expected.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fem::io::detail {

// Reads a .msh file held in memory, in ASCII or binary mode, and knows where
// it is: the line (ASCII) or the byte (binary) used in error messages
// (REQ-003-22).
class GmshCursor
{
public:
    GmshCursor(std::string_view data, std::string_view sourceName);

    // Binary mode reads raw little-endian values; ASCII mode reads
    // whitespace-separated tokens. Section headers are always ASCII lines.
    void setBinary(bool binary) noexcept { m_binary = binary; }
    [[nodiscard]] bool isBinary() const noexcept { return m_binary; }

    [[nodiscard]] bool atEnd() const noexcept;
    void skipWhitespace() noexcept;

    // Rest of the current line, without the line break; moves to the next line.
    [[nodiscard]] core::Expected<std::string_view> line();
    // Next whitespace-separated word (ASCII only).
    [[nodiscard]] core::Expected<std::string_view> token();

    // Gmsh "int" (4 bytes in binary), "size_t" (8 bytes) and "double".
    [[nodiscard]] core::Expected<std::int32_t> readInt();
    [[nodiscard]] core::Expected<std::uint64_t> readSize();
    [[nodiscard]] core::Expected<double> readDouble();

    // Skips `count` values of the given kind without converting them.
    [[nodiscard]] core::Expected<void> skipSizes(std::uint64_t count);
    [[nodiscard]] core::Expected<void> skipDoubles(std::uint64_t count);

    // Moves past the line "$End<name>", skipping anything before it.
    [[nodiscard]] core::Expected<void> skipSection(std::string_view name);
    // Expects the line "$End<name>" after optional whitespace.
    [[nodiscard]] core::Expected<void> expectEnd(std::string_view name);

    // "line 12" (ASCII) or "byte 345" (binary) for the current position.
    [[nodiscard]] std::string position() const;
    // "<source>, line 12: <what>".
    [[nodiscard]] core::Error error(std::string_view what) const;

    [[nodiscard]] std::size_t offset() const noexcept { return m_pos; }

private:
    template <typename T>
    [[nodiscard]] core::Expected<T> readRaw();
    [[nodiscard]] core::Expected<std::int64_t> readInteger();

    std::string_view m_data;
    std::string m_source;
    std::size_t m_pos = 0;
    bool m_binary = false;
};

// Converts text to double independently of the C locale. Exposed for tests.
[[nodiscard]] bool parseDouble(std::string_view text, double& value);

} // namespace fem::io::detail
