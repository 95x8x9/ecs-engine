#pragma once

#include <cstddef>

#include "Boids/Common/boid_parameters.h"
#include "Boids/Common/vector_math.h"

namespace boids {

// The caller supplies at least one neighbor, accumulated in traversal order.
// Limit alignment steering before applying its weight to total acceleration.
[[nodiscard]] inline glm::vec3 CalculateAlignment(
    const glm::vec3& velocity_sum, std::size_t neighbor_count,
    const glm::vec3& self_velocity, const BoidParameters& parameters) {
  constexpr float kMinVectorLengthSquared = 1e-8F;
  const glm::vec3 average = velocity_sum / static_cast<float>(neighbor_count);
  if (glm::dot(average, average) <= kMinVectorLengthSquared) {
    return glm::vec3{0.0F};
  }
  // Do not skip validation when the force limit is zero: overflow must fail
  // under the same conditions in every implementation.
  const glm::vec3 desired =
      NormalizeNonzero(average) * parameters.preferred_speed;
  return LimitMagnitude(desired - self_velocity,
                        parameters.max_alignment_force);
}

}  // namespace boids
