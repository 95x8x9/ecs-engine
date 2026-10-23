#pragma once

#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>

#include "Boids/Common/boid_simulation_config.h"

namespace boids {

// Storage-specific object/Entity limits remain the implementation's concern.
inline void ValidateSimulationConfig(const BoidSimulationConfig& config) {
  if (config.dummy_change_percent > 100U) {
    throw std::invalid_argument("Dummy change percent must be in [0, 100]");
  }
  if (!std::isfinite(config.boundary_half_extent) ||
      config.boundary_half_extent <= 0.0F) {
    throw std::invalid_argument(
        "Boundary half extent must be finite and positive");
  }
  if (!std::isfinite(config.spawn_half_extent) ||
      config.spawn_half_extent < 0.0F ||
      config.spawn_half_extent > config.boundary_half_extent ||
      config.spawn_half_extent > std::numeric_limits<float>::max() / 2.0F) {
    throw std::invalid_argument(
        "Spawn range must be finite, representable and inside the boundary");
  }
  const auto& parameters = config.parameters;
  for (const float value :
       {config.initial_speed, parameters.neighbor_radius,
        parameters.separation_radius, parameters.separation_weight,
        parameters.alignment_weight, parameters.cohesion_weight,
        parameters.preferred_speed, parameters.max_speed,
        parameters.max_alignment_force}) {
    if (!std::isfinite(value) || value < 0.0F) {
      throw std::invalid_argument(
          "Initial speed and Boid parameters must be finite and nonnegative");
    }
  }
}

inline void ValidateDeltaTime(float dt) {
  if (!std::isfinite(dt) || dt < 0.0F) {
    throw std::invalid_argument("Delta time must be finite and nonnegative");
  }
}

}  // namespace boids
