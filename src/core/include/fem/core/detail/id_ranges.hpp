#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace fem::core::detail {

// Maps external ids (numbers from a source file) to contiguous indices and
// back, storing runs of consecutive ids as a single range. Files from Gmsh
// number nodes and elements consecutively, so a whole mesh usually needs a
// handful of ranges instead of one entry per item (NFR-003-01).
//
// Not part of the public API; it lives in a header only because Mesh holds it.
class IdRanges
{
public:
    // Assigns the next index (size()) to `id`.
    void append(std::int64_t id);
    // Appends every id of `other`, with indices shifted by `indexOffset`.
    // `indexOffset` must equal size().
    void appendAll(const IdRanges& other);

    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] std::int64_t idAt(std::int32_t index) const;

    // Prepares find(). Returns the first id used more than once, if any.
    [[nodiscard]] std::optional<std::int64_t> seal();
    [[nodiscard]] bool isSealed() const noexcept { return m_sealed; }
    // Requires seal().
    [[nodiscard]] std::optional<std::int32_t> find(std::int64_t id) const;

    void shrinkToFit();
    [[nodiscard]] std::size_t memoryUsage() const noexcept;

    friend bool operator==(const IdRanges& a, const IdRanges& b) noexcept
    {
        return a.m_byIndex == b.m_byIndex;
    }

private:
    struct Range
    {
        std::int64_t firstId = 0;
        std::int32_t firstIndex = 0;
        std::int32_t count = 0;

        friend bool operator==(const Range&, const Range&) = default;
    };

    std::vector<Range> m_byIndex; // in index order
    std::vector<Range> m_byId;    // sorted by id, filled by seal()
    std::size_t m_size = 0;
    bool m_sealed = false;
};

} // namespace fem::core::detail
