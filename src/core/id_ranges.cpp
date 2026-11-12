#include <fem/core/detail/id_ranges.hpp>

#include <algorithm>
#include <cassert>

namespace fem::core::detail {

void IdRanges::append(std::int64_t id)
{
    assert(m_size < static_cast<std::size_t>(INT32_MAX));
    const auto index = static_cast<std::int32_t>(m_size);
    if (!m_byIndex.empty()) {
        Range& last = m_byIndex.back();
        if (last.firstId + last.count == id) {
            ++last.count;
            ++m_size;
            m_sealed = false;
            return;
        }
    }
    m_byIndex.push_back({.firstId = id, .firstIndex = index, .count = 1});
    ++m_size;
    m_sealed = false;
}

void IdRanges::appendAll(const IdRanges& other)
{
    const auto offset = static_cast<std::int32_t>(m_size);
    for (const Range& range : other.m_byIndex) {
        if (!m_byIndex.empty() &&
            m_byIndex.back().firstId + m_byIndex.back().count == range.firstId) {
            m_byIndex.back().count += range.count;
        } else {
            m_byIndex.push_back({.firstId = range.firstId,
                                 .firstIndex = range.firstIndex + offset,
                                 .count = range.count});
        }
    }
    m_size += other.m_size;
    m_sealed = false;
}

std::int64_t IdRanges::idAt(std::int32_t index) const
{
    assert(index >= 0 && static_cast<std::size_t>(index) < m_size);
    // Last range whose first index is <= index.
    const auto it = std::ranges::upper_bound(m_byIndex, index, {}, &Range::firstIndex);
    const Range& range = *(it - 1);
    return range.firstId + (index - range.firstIndex);
}

std::optional<std::int64_t> IdRanges::seal()
{
    m_byId = m_byIndex;
    std::ranges::sort(m_byId, {}, &Range::firstId);
    m_sealed = true;
    for (std::size_t i = 1; i < m_byId.size(); ++i) {
        const Range& previous = m_byId[i - 1];
        if (previous.firstId + previous.count > m_byId[i].firstId) {
            return m_byId[i].firstId;
        }
    }
    return std::nullopt;
}

std::optional<std::int32_t> IdRanges::find(std::int64_t id) const
{
    assert(m_sealed);
    const auto it = std::ranges::upper_bound(m_byId, id, {}, &Range::firstId);
    if (it == m_byId.begin()) {
        return std::nullopt;
    }
    const Range& range = *(it - 1);
    if (id >= range.firstId + range.count) {
        return std::nullopt;
    }
    return range.firstIndex + static_cast<std::int32_t>(id - range.firstId);
}

void IdRanges::shrinkToFit()
{
    m_byIndex.shrink_to_fit();
    m_byId.shrink_to_fit();
}

std::size_t IdRanges::memoryUsage() const noexcept
{
    return (m_byIndex.capacity() + m_byId.capacity()) * sizeof(Range);
}

} // namespace fem::core::detail
