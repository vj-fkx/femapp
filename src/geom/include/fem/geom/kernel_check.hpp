#pragma once

#include <string>

// Minimal checks that the geometric kernel is linked and working.
// This header must not include any header of the kernel itself.
namespace fem::geom {

// Builds a dx * dy * dz box in the geometric kernel and returns its volume.
[[nodiscard]] double boxVolume(double dx, double dy, double dz);

// Version of the geometric kernel, e.g. "7.9.1".
[[nodiscard]] std::string kernelVersion();

} // namespace fem::geom
