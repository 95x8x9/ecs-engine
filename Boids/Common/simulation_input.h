#pragma once

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

#include "Boids/Common/boid_simulation_config.h"

namespace boids {

enum class ObjectKind { Boid, Dummy };

struct InitialObjectState {
  ObjectKind kind;
  // Index in initial_objects; retained when a Dummy is recreated.
  std::size_t logical_id;
  glm::vec3 position;
  glm::vec3 velocity;
};

using DummyStepPlan = std::vector<std::size_t>;

struct SimulationInput {
  // Actual creation order, with logical_id equal to the initial index.
  std::vector<InitialObjectState> initial_objects;
  // dummy_steps[s] lists the logical Dummy IDs to replace in step s.
  std::vector<DummyStepPlan> dummy_steps;
};

// Prepare before initialization measurement. The caller owns the input through
// the last simulation update and storage report; simulations borrow it unchanged.
[[nodiscard]] SimulationInput PrepareSimulationInput(
    const BoidSimulationConfig& config, std::size_t total_steps);

}  // namespace boids
