#pragma once

// Feature 012 (design §5.5; REQ-012-05, 10, NFR-012-04): the samples of a
// result made ready for the GPU: in float, relative to the centre of the
// bars, and the segments between consecutive samples of each bar.

#include <fem/core/mesh.hpp>
#include <fem/core/result_sampling.hpp>

#include <Eigen/Core>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace fem::render {

// No sample (nodeSamples() of a node that no bar reaches).
inline constexpr std::uint32_t kNoSample = 0xFFFFFFFFU;

// The bytes per segment on the GPU: its two ends (position and
// displacement, 4 x vec3), their two values and its flags (design §5.5).
inline constexpr std::size_t kSegmentGpuBytes = 48 + 8 + 4;

// Built on a worker thread from core::ShapeSamples, immutable afterwards
// (like MeshScene).
class ResultScene
{
public:
    [[nodiscard]] const std::shared_ptr<const core::Mesh>& mesh() const noexcept { return m_mesh; }
    // Positions are relative to this point, the centre of the bars
    // (REQ-004-18).
    [[nodiscard]] const Eigen::Vector3d& origin() const noexcept { return m_origin; }
    [[nodiscard]] const core::BoundingBox& bounds() const noexcept { return m_bounds; }

    // Per sample.
    [[nodiscard]] std::span<const Eigen::Vector3f> positions() const noexcept
    {
        return m_positions;
    }
    [[nodiscard]] std::span<const Eigen::Vector3f> displacements() const noexcept
    {
        return m_displacements;
    }
    [[nodiscard]] std::span<const core::Station> stations() const noexcept { return m_stations; }
    [[nodiscard]] std::span<const core::SampledBar> bars() const noexcept { return m_bars; }

    // Per segment: from sample segmentStarts()[s] to the next one, of the
    // element segmentElements()[s]. The two sides of a concentrated load
    // (no length) make no segment.
    [[nodiscard]] std::span<const std::uint32_t> segmentStarts() const noexcept
    {
        return m_segmentStarts;
    }
    [[nodiscard]] std::span<const core::ElementIndex> segmentElements() const noexcept
    {
        return m_segmentElements;
    }

    // Per node of the mesh: the first sample at it, or kNoSample.
    [[nodiscard]] std::span<const std::uint32_t> nodeSamples() const noexcept
    {
        return m_nodeSamples;
    }

    // NFR-012-04: what the segments take on the GPU.
    [[nodiscard]] std::size_t gpuBytes() const noexcept
    {
        return m_segmentStarts.size() * kSegmentGpuBytes;
    }

private:
    friend std::shared_ptr<const ResultScene>
    buildResultScene(const core::ShapeSamples& shape, std::shared_ptr<const core::Mesh> mesh);

    std::shared_ptr<const core::Mesh> m_mesh;
    Eigen::Vector3d m_origin = Eigen::Vector3d::Zero();
    core::BoundingBox m_bounds;
    std::vector<Eigen::Vector3f> m_positions;
    std::vector<Eigen::Vector3f> m_displacements;
    std::vector<core::Station> m_stations;
    std::vector<core::SampledBar> m_bars;
    std::vector<std::uint32_t> m_segmentStarts;
    std::vector<core::ElementIndex> m_segmentElements;
    std::vector<std::uint32_t> m_nodeSamples;
};

[[nodiscard]] std::shared_ptr<const ResultScene>
buildResultScene(const core::ShapeSamples& shape, std::shared_ptr<const core::Mesh> mesh);

// REQ-012-13: the values of a quantity on the segments, two per segment
// (its start, then its end), in float; NaN where there is no value.
[[nodiscard]] std::shared_ptr<const std::vector<float>>
segmentValues(const ResultScene& scene, const core::FieldSamples& field);

} // namespace fem::render
