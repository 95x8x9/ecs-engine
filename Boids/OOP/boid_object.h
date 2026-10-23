#pragma once

#include <cstddef>
#include <memory>
#include <span>

#include "Boids/Common/boid_parameters.h"
#include "Boids/OOP/simulation_object.h"

namespace boids::oop {

class BoidObject final : public SimulationObject {
 public:
  BoidObject(const boids::InitialObjectState& initial,
             const boids::BoidParameters& parameters)
      : SimulationObject(boids::ObjectKind::Boid, initial.logical_id,
                         initial.position, initial.velocity),
        parameters_(parameters) {}
  void CalculateAcceleration(
      std::span<const std::unique_ptr<SimulationObject>> objects,
      std::span<const std::size_t> object_indices);
  void Move(float delta_seconds);
  void ApplyBoundary(float boundary_half_extent);
  void UpdateOrientation();

  [[nodiscard]] const glm::vec3& Acceleration() const { return acceleration_; }

 private:
  glm::vec3 acceleration_{0.0F};
  boids::BoidParameters parameters_;
};

}  // namespace boids::oop
