#pragma once

#include <algorithm>
#include <cstddef>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

#include "Boids/Common/boid_simulation_config.h"
#include "Boids/Common/simulation_validation.h"

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
// the last simulation update and storage report; simulations borrow it
// unchanged. total_steps includes warmup and measurement; measurement continues
// the plan. Reproducibility assumes the same standard library implementation.
[[nodiscard]] inline SimulationInput PrepareSimulationInput(
    const BoidSimulationConfig& config, std::size_t total_steps) {
  ValidateSimulationConfig(config);

  const auto checked_add = [](std::size_t left, std::size_t right) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
      throw std::overflow_error("Simulation input size overflow");
    }
    return left + right;
  };
  const auto checked_multiply = [](std::size_t left, std::size_t right) {
    if (right != 0U && left > std::numeric_limits<std::size_t>::max() / right) {
      throw std::overflow_error("Simulation input size overflow");
    }
    return left * right;
  };

  const std::size_t object_count =
      checked_add(config.boid_count, config.dummy_count);
  // Divide first so even dummy_count * percent cannot overflow.
  const std::size_t change_count =
      (config.dummy_count / 100U) * config.dummy_change_percent +
      ((config.dummy_count % 100U) * config.dummy_change_percent) / 100U;
  SimulationInput input;
  std::vector<std::size_t> dummy_ids;
  if (object_count > input.initial_objects.max_size() ||
      total_steps > input.dummy_steps.max_size() ||
      config.dummy_count > dummy_ids.max_size()) {
    throw std::overflow_error("Simulation input exceeds container size limits");
  }

  // Check the entire plan, including its per-step vectors, before any
  // allocation.
  const auto plan_entries = checked_multiply(total_steps, change_count);
  const auto plan_bytes =
      checked_add(checked_multiply(total_steps, sizeof(DummyStepPlan)),
                  checked_multiply(plan_entries, sizeof(std::size_t)));
  const auto input_bytes = checked_add(
      checked_multiply(object_count, sizeof(InitialObjectState)), plan_bytes);
  const bool needs_plan = total_steps != 0U && change_count != 0U;
  static_cast<void>(checked_add(
      input_bytes,
      needs_plan ? checked_multiply(config.dummy_count, sizeof(std::size_t))
                 : 0U));

  input.initial_objects.reserve(object_count);
  input.dummy_steps.resize(total_steps);
  if (needs_plan) {
    dummy_ids.reserve(config.dummy_count);
  }

  std::mt19937 initial_random{config.random_seed};
  std::uniform_real_distribution<float> position_distribution(
      -config.spawn_half_extent, config.spawn_half_extent);
  std::uniform_real_distribution<float> direction_distribution(-1.0F, 1.0F);
  const auto append_object = [&](ObjectKind kind) {
    const std::size_t logical_id = input.initial_objects.size();
    const glm::vec3 position{position_distribution(initial_random),
                             position_distribution(initial_random),
                             position_distribution(initial_random)};
    glm::vec3 direction{direction_distribution(initial_random),
                        direction_distribution(initial_random),
                        direction_distribution(initial_random)};
    if (glm::dot(direction, direction) <= 1e-8F) {
      direction = glm::vec3{1.0F, 0.0F, 0.0F};
    } else {
      direction = glm::normalize(direction);
    }
    input.initial_objects.push_back(
        {kind, logical_id, position, direction * config.initial_speed});
    if (needs_plan && kind == ObjectKind::Dummy) {
      dummy_ids.push_back(logical_id);
    }
  };

  const std::size_t paired_count =
      std::min(config.boid_count, config.dummy_count);
  for (std::size_t index = 0; index < paired_count; ++index) {
    append_object(ObjectKind::Boid);
    append_object(ObjectKind::Dummy);
  }
  for (std::size_t index = paired_count; index < config.boid_count; ++index) {
    append_object(ObjectKind::Boid);
  }
  for (std::size_t index = paired_count; index < config.dummy_count; ++index) {
    append_object(ObjectKind::Dummy);
  }

  if (needs_plan) {
    std::mt19937 plan_random{config.random_seed};
    for (auto& step : input.dummy_steps) {
      step.reserve(change_count);
      std::sample(dummy_ids.begin(), dummy_ids.end(), std::back_inserter(step),
                  change_count, plan_random);
      std::sort(step.begin(), step.end());
    }
  }
  return input;
}

}  // namespace boids
