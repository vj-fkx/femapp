#include <fem/core/detail/id_ranges.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <vector>

using fem::core::detail::IdRanges;

// REQ-003-04, REQ-003-06
TEST_CASE("id ranges map ids to indices and back", "[core][ids]")
{
    IdRanges ids;
    // Two runs, a lone id and ids out of order.
    const std::vector<std::int64_t> input = {10, 11, 12, 13, 100, 101, 7, 50, 51, 52};
    for (const std::int64_t id : input) {
        ids.append(id);
    }
    REQUIRE(ids.size() == input.size());
    REQUIRE_FALSE(ids.seal().has_value());

    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto index = static_cast<std::int32_t>(i);
        CHECK(ids.idAt(index) == input[i]);
        CHECK(ids.find(input[i]) == std::optional<std::int32_t>(index));
    }
    CHECK_FALSE(ids.find(9).has_value());
    CHECK_FALSE(ids.find(14).has_value());
    CHECK_FALSE(ids.find(53).has_value());
    CHECK_FALSE(ids.find(-1).has_value());
}

// NFR-003-01: consecutive ids cost a single range.
TEST_CASE("consecutive ids are stored as one range", "[core][ids]")
{
    IdRanges compact;
    IdRanges scattered;
    for (std::int64_t i = 0; i < 10000; ++i) {
        compact.append(i + 1);
        scattered.append(3 * i + 1);
    }
    (void)compact.seal();
    (void)scattered.seal();
    CHECK(compact.memoryUsage() < 100);
    CHECK(scattered.memoryUsage() > 10000 * 16);
}

// REQ-003-10
TEST_CASE("repeated ids are detected", "[core][ids]")
{
    IdRanges ids;
    for (const std::int64_t id : {1, 2, 3, 4, 5, 20, 21, 3}) {
        ids.append(id);
    }
    CHECK(ids.seal() == std::optional<std::int64_t>(3));

    IdRanges overlapping;
    for (const std::int64_t id : {10, 11, 12, 5, 6, 7, 8, 9, 10}) {
        overlapping.append(id);
    }
    CHECK(overlapping.seal() == std::optional<std::int64_t>(10));
}

// REQ-003-06: blocks of different element types are concatenated.
TEST_CASE("id ranges can be concatenated", "[core][ids]")
{
    IdRanges first;
    IdRanges second;
    for (const std::int64_t id : {1, 2, 3}) {
        first.append(id);
    }
    for (const std::int64_t id : {4, 5, 40}) {
        second.append(id);
    }
    IdRanges all;
    all.appendAll(first);
    all.appendAll(second);
    REQUIRE(all.size() == 6);
    REQUIRE_FALSE(all.seal().has_value());
    CHECK(all.idAt(3) == 4);
    CHECK(all.idAt(5) == 40);
    CHECK(all.find(40) == std::optional<std::int32_t>(5));
}
