#include "Boids/OOP/boid_object.h"

#include <cmath>
#include <glm/geometric.hpp>

#include "Boids/Common/alignment.h"
#include "Boids/Common/vector_math.h"

namespace boids::oop {

void BoidObject::CalculateAcceleration(std::span<const BoidObject> boids) {
  glm::vec3 separation{0.0F};
  glm::vec3 velocity_sum{0.0F};
  glm::vec3 position_sum{0.0F};
  std::size_t neighbor_count = 0U;

  constexpr float kMinDistanceSquared = 0.00000001F;
  for (const auto& other : boids) {
    if (&other == this) {
      continue;
    }

    const glm::vec3 offset = other.transform_.position - transform_.position;
    const float distance_squared = glm::dot(offset, offset);
    if (!boids::WithinRadius(distance_squared, transform_.position,
                             other.transform_.position,
                             parameters_.neighbor_radius)) {
      continue;
    }

    ++neighbor_count;
    velocity_sum += other.velocity_;
    position_sum += other.transform_.position;

    if (distance_squared > kMinDistanceSquared &&
        boids::WithinRadius(distance_squared, transform_.position,
                            other.transform_.position,
                            parameters_.separation_radius)) {
      separation -= boids::InverseSquareOffset(offset, distance_squared);
    }
  }

  if (neighbor_count == 0U) {
    acceleration_ = glm::vec3{0.0F};
    return;
  }

  const float count = static_cast<float>(neighbor_count);
  const glm::vec3 alignment = boids::CalculateAlignment(
      velocity_sum, neighbor_count, velocity_, parameters_);
  const glm::vec3 cohesion = position_sum / count - transform_.position;
  acceleration_ =
      boids::RequireFinite(separation * parameters_.separation_weight +
                           alignment * parameters_.alignment_weight +
                           cohesion * parameters_.cohesion_weight);
}

void BoidObject::Move(float delta_seconds) {
  velocity_ += acceleration_ * delta_seconds;
  velocity_ = boids::LimitSpeed(velocity_, parameters_.max_speed);
  transform_.position =
      boids::RequireFinite(transform_.position + velocity_ * delta_seconds);
}

void BoidObject::ApplyBoundary(float half_extent) {
  for (int axis = 0; axis < 3; ++axis) {
    if (transform_.position[axis] < -half_extent) {
      transform_.position[axis] = -half_extent;
      velocity_[axis] = std::abs(velocity_[axis]);
    } else if (transform_.position[axis] > half_extent) {
      transform_.position[axis] = half_extent;
      velocity_[axis] = -std::abs(velocity_[axis]);
    }
  }
}

void BoidObject::UpdateOrientation() {
  transform_.rotation = engine::renderer::RotationFromDirection(velocity_);
}

}  // namespace boids::oop
