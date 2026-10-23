#pragma once

#include <cmath>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <limits>
#include <stdexcept>

namespace boids {

[[nodiscard]] inline glm::vec3 RequireFinite(const glm::vec3& value) {
  if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
      !std::isfinite(value.z)) {
    throw std::overflow_error("Boids arithmetic exceeded finite float range");
  }
  return value;
}

// Keep the float calculation when its squared length is normal. Double handles
// finite vectors whose squares overflow or underflow, including subnormals.
[[nodiscard]] inline glm::vec3 NormalizeNonzero(const glm::vec3& value) {
  if (std::isnormal(glm::dot(value, value))) return glm::normalize(value);
  return glm::vec3{glm::normalize(glm::dvec3{RequireFinite(value)})};
}

namespace detail {

[[nodiscard]] inline glm::vec3 LimitMagnitudeWide(const glm::vec3& value,
                                                  float maximum) {
  const glm::dvec3 wide{value};
  const double length = glm::length(wide);
  if (!std::isfinite(length)) {
    throw std::overflow_error("Cannot limit a non-finite Boids vector");
  }
  if (length <= static_cast<double>(maximum)) return value;
  auto limited = glm::vec3{(wide / length) * static_cast<double>(maximum)};
  // At subnormal limits, rounding each component up can exceed the requested
  // magnitude substantially. One step toward zero puts each below its exact
  // value; the smallest limits may have no nonzero vector in this direction.
  if (maximum < std::numeric_limits<float>::min() &&
      glm::length(glm::dvec3{limited}) > static_cast<double>(maximum)) {
    for (int axis = 0; axis < 3; ++axis)
      limited[axis] = std::nextafter(limited[axis], 0.0F);
  }
  return limited;
}

}  // namespace detail

[[nodiscard]] inline glm::vec3 LimitMagnitude(const glm::vec3& value,
                                              float maximum) {
  if (maximum <= 0.0F) {
    static_cast<void>(RequireFinite(value));
    return glm::vec3{0.0F};
  }
  const float squared_length = glm::dot(value, value);
  const float squared_maximum = maximum * maximum;
  if (!std::isnormal(squared_maximum) ||
      (!std::isnormal(squared_length) && value != glm::vec3{0.0F})) {
    return detail::LimitMagnitudeWide(value, maximum);
  }
  if (squared_length <= squared_maximum) return value;
  return glm::normalize(value) * maximum;
}

// Movement historically compares length, while alignment compares squared
// length. Keep their float rounding and operation order for ordinary inputs.
[[nodiscard]] inline glm::vec3 LimitSpeed(const glm::vec3& velocity,
                                          float maximum) {
  const float squared_length = glm::dot(velocity, velocity);
  const float speed = std::sqrt(squared_length);
  if (maximum <= 0.0F) {
    static_cast<void>(RequireFinite(velocity));
    return glm::vec3{0.0F};
  }
  if ((!std::isnormal(squared_length) && velocity != glm::vec3{0.0F}) ||
      maximum < std::numeric_limits<float>::min())
    return detail::LimitMagnitudeWide(velocity, maximum);
  if (speed > maximum) return glm::normalize(velocity) * maximum;
  return velocity;
}

// Recompute the difference in double when the radius square cannot represent
// the cutoff. In particular, inf < inf and 0 < 0 must not drop valid neighbors.
[[nodiscard]] inline bool WithinRadius(float squared_distance,
                                       const glm::vec3& self,
                                       const glm::vec3& other, float radius) {
  const float squared_radius = radius * radius;
  if (std::isnormal(squared_radius)) return squared_distance < squared_radius;
  if (radius <= 0.0F) return false;
  const auto offset = glm::dvec3{other} - glm::dvec3{self};
  return glm::dot(offset, offset) < static_cast<double>(radius) * radius;
}

// Called only for noncoincident neighbors inside the finite separation radius.
[[nodiscard]] inline glm::vec3 InverseSquareOffset(const glm::vec3& offset,
                                                   float squared_distance) {
  if (std::isfinite(squared_distance)) return offset / squared_distance;
  const glm::dvec3 wide{offset};
  return glm::vec3{wide / glm::dot(wide, wide)};
}

}  // namespace boids
