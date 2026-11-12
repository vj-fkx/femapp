#include "gmsh_cursor.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <system_error>

#if !(defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L)
#include <array>
#include <clocale>
#include <cstdlib>
#if defined(__APPLE__)
#include <xlocale.h>
#endif
#endif

namespace fem::io::detail {

namespace {

constexpr std::size_t kMaxNumberLength = 64;

bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

} // namespace

bool parseDouble(std::string_view text, double& value)
{
    if (text.empty()) {
        return false;
    }
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    // Locale independent and correctly rounded.
    const char* first = text.data();
    const char* last = text.data() + text.size();
    if (*first == '+') {
        ++first;
    }
    const auto [end, error] = std::from_chars(first, last, value);
    return error == std::errc() && end == last;
#else
    // Older standard libraries (e.g. Apple's libc++) lack from_chars for
    // double: use strtod with the "C" locale, which is also correctly rounded.
    if (text.size() >= kMaxNumberLength) {
        return false;
    }
    std::array<char, kMaxNumberLength> buffer{};
    std::copy(text.begin(), text.end(), buffer.begin());
    static const locale_t cLocale = newlocale(LC_ALL_MASK, "C", nullptr);
    char* end = nullptr;
    value = strtod_l(buffer.data(), &end, cLocale);
    return end == buffer.data() + text.size();
#endif
}

GmshCursor::GmshCursor(std::string_view data, std::string_view sourceName)
    : m_data(data)
    , m_source(sourceName)
{}

bool GmshCursor::atEnd() const noexcept
{
    return m_pos >= m_data.size();
}

void GmshCursor::skipWhitespace() noexcept
{
    while (m_pos < m_data.size() && isSpace(m_data[m_pos])) {
        ++m_pos;
    }
}

core::Expected<std::string_view> GmshCursor::line()
{
    if (atEnd()) {
        return tl::unexpected(error("unexpected end of file"));
    }
    const std::size_t end = m_data.find('\n', m_pos);
    const std::size_t stop = end == std::string_view::npos ? m_data.size() : end;
    std::string_view result = m_data.substr(m_pos, stop - m_pos);
    if (!result.empty() && result.back() == '\r') {
        result.remove_suffix(1);
    }
    m_pos = end == std::string_view::npos ? m_data.size() : end + 1;
    return result;
}

core::Expected<std::string_view> GmshCursor::token()
{
    skipWhitespace();
    if (atEnd()) {
        return tl::unexpected(error("unexpected end of file"));
    }
    const std::size_t start = m_pos;
    while (m_pos < m_data.size() && !isSpace(m_data[m_pos])) {
        ++m_pos;
    }
    return m_data.substr(start, m_pos - start);
}

template <typename T>
core::Expected<T> GmshCursor::readRaw()
{
    if (m_data.size() - m_pos < sizeof(T)) {
        m_pos = m_data.size();
        return tl::unexpected(error("unexpected end of file"));
    }
    T value{};
    std::memcpy(&value, m_data.data() + m_pos, sizeof(T));
    m_pos += sizeof(T);
    return value;
}

core::Expected<std::int64_t> GmshCursor::readInteger()
{
    const std::size_t start = m_pos;
    const auto text = token();
    if (!text) {
        return tl::unexpected(text.error());
    }
    std::int64_t value = 0;
    const char* first = text->data();
    const char* last = text->data() + text->size();
    const auto [end, code] = std::from_chars(first, last, value);
    if (code != std::errc() || end != last) {
        m_pos = start;
        skipWhitespace();
        return tl::unexpected(error("invalid integer '" + std::string(*text) + "'"));
    }
    return value;
}

core::Expected<std::int32_t> GmshCursor::readInt()
{
    if (m_binary) {
        return readRaw<std::int32_t>();
    }
    const auto value = readInteger();
    if (!value) {
        return tl::unexpected(value.error());
    }
    if (*value < INT32_MIN || *value > INT32_MAX) {
        return tl::unexpected(error("integer out of range"));
    }
    return static_cast<std::int32_t>(*value);
}

core::Expected<std::uint64_t> GmshCursor::readSize()
{
    if (m_binary) {
        return readRaw<std::uint64_t>();
    }
    const auto value = readInteger();
    if (!value) {
        return tl::unexpected(value.error());
    }
    if (*value < 0) {
        return tl::unexpected(error("negative count or number"));
    }
    return static_cast<std::uint64_t>(*value);
}

core::Expected<double> GmshCursor::readDouble()
{
    if (m_binary) {
        return readRaw<double>();
    }
    const std::size_t start = m_pos;
    const auto text = token();
    if (!text) {
        return tl::unexpected(text.error());
    }
    double value = 0.0;
    if (!parseDouble(*text, value)) {
        m_pos = start;
        skipWhitespace();
        return tl::unexpected(error("invalid number '" + std::string(*text) + "'"));
    }
    return value;
}

core::Expected<void> GmshCursor::skipSizes(std::uint64_t count)
{
    if (m_binary) {
        if ((m_data.size() - m_pos) / sizeof(std::uint64_t) < count) {
            m_pos = m_data.size();
            return tl::unexpected(error("unexpected end of file"));
        }
        m_pos += static_cast<std::size_t>(count) * sizeof(std::uint64_t);
        return {};
    }
    for (std::uint64_t i = 0; i < count; ++i) {
        if (const auto value = readSize(); !value) {
            return tl::unexpected(value.error());
        }
    }
    return {};
}

core::Expected<void> GmshCursor::skipDoubles(std::uint64_t count)
{
    if (m_binary) {
        if ((m_data.size() - m_pos) / sizeof(double) < count) {
            m_pos = m_data.size();
            return tl::unexpected(error("unexpected end of file"));
        }
        m_pos += static_cast<std::size_t>(count) * sizeof(double);
        return {};
    }
    for (std::uint64_t i = 0; i < count; ++i) {
        if (const auto value = readDouble(); !value) {
            return tl::unexpected(value.error());
        }
    }
    return {};
}

core::Expected<void> GmshCursor::skipSection(std::string_view name)
{
    const std::string marker = "$End" + std::string(name);
    while (!atEnd()) {
        const auto text = line();
        if (!text) {
            return tl::unexpected(text.error());
        }
        if (*text == marker) {
            return {};
        }
    }
    return tl::unexpected(error("missing " + marker));
}

core::Expected<void> GmshCursor::expectEnd(std::string_view name)
{
    skipWhitespace();
    const std::string marker = "$End" + std::string(name);
    const std::size_t start = m_pos;
    const auto text = line();
    if (!text || *text != marker) {
        m_pos = start;
        return tl::unexpected(error("expected " + marker));
    }
    return {};
}

std::string GmshCursor::position() const
{
    const std::size_t at = std::min(m_pos, m_data.size());
    if (m_binary) {
        return "byte " + std::to_string(at);
    }
    const auto lineNumber =
        std::count(m_data.begin(), m_data.begin() + static_cast<std::ptrdiff_t>(at), '\n') + 1;
    return "line " + std::to_string(lineNumber);
}

core::Error GmshCursor::error(std::string_view what) const
{
    return core::Error{.message = m_source + ", " + position() + ": " + std::string(what)};
}

} // namespace fem::io::detail
