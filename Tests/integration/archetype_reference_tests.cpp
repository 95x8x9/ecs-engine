#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "Boids/Archetype/boid_simulation.h"
#include "Boids/ECS/Components/acceleration.h"
#include "Boids/ECS/Components/velocity.h"
#include "Engine/Measurement/timing_recorder.h"
#include "Engine/Renderer/transform.h"

namespace {

using boids::ecs::Acceleration;
using boids::ecs::Velocity;
using engine::renderer::Transform;

struct State {
  glm::dvec3 position;
  glm::dvec3 velocity;
  glm::dvec3 acceleration;
  glm::quat rotation;
};

std::vector<State> Snapshot(const boids::archetype::BoidSimulation& simulation,
                            const boids::SimulationInput& input) {
  const auto& table = *simulation.GetRegistry()
                           .FindTable<Transform, Velocity, Acceleration,
                                      boids::BoidParameters>();
  std::vector<State> states;
  for (const auto& initial : input.initial_objects) {
    if (initial.kind != boids::ObjectKind::Boid) continue;
    const auto entities = table.Entities();
    const auto found = std::find(entities.begin(), entities.end(),
                                 simulation.EntityFor(initial.logical_id));
    if (found == entities.end()) throw std::logic_error("Missing logical Boid");
    const auto row = static_cast<std::size_t>(found - entities.begin());
    states.push_back({glm::dvec3{table.Column<Transform>()[row].position},
                      glm::dvec3{table.Column<Velocity>()[row].value},
                      glm::dvec3{table.Column<Acceleration>()[row].value},
                      table.Column<Transform>()[row].rotation});
  }
  return states;
}

// Independent equations in double: do not call the production math,
// BoidObject, systems, or RotationFromDirection to construct expected states.
std::vector<State> ReferenceStep(const std::vector<State>& before,
                                 const boids::BoidSimulationConfig& config,
                                 float dt) {
  auto after = before;
  const auto& p = config.parameters;
  const double radius2 =
      static_cast<double>(p.neighbor_radius) * p.neighbor_radius;
  const double separation2 =
      static_cast<double>(p.separation_radius) * p.separation_radius;
  for (std::size_t self = 0; self < before.size(); ++self) {
    glm::dvec3 separation{0}, velocity_sum{0}, position_sum{0};
    std::size_t count = 0;
    for (std::size_t other = 0; other < before.size(); ++other) {
      if (other == self) continue;
      const auto offset = before[other].position - before[self].position;
      const double distance2 = glm::dot(offset, offset);
      if (distance2 >= radius2) continue;
      ++count;
      velocity_sum += before[other].velocity;
      position_sum += before[other].position;
      if (distance2 > static_cast<double>(1e-8F) && distance2 < separation2)
        separation -= offset / distance2;
    }
    glm::dvec3 acceleration{0};
    if (count > 0) {
      const auto average = velocity_sum / static_cast<double>(count);
      glm::dvec3 alignment{0};
      if (glm::dot(average, average) > static_cast<double>(1e-8F) &&
          p.max_alignment_force > 0) {
        alignment = average / glm::length(average) *
                        static_cast<double>(p.preferred_speed) -
                    before[self].velocity;
        const auto length = glm::length(alignment);
        if (length > p.max_alignment_force)
          alignment *= static_cast<double>(p.max_alignment_force) / length;
      }
      const auto cohesion =
          position_sum / static_cast<double>(count) - before[self].position;
      acceleration = separation * static_cast<double>(p.separation_weight) +
                     alignment * static_cast<double>(p.alignment_weight) +
                     cohesion * static_cast<double>(p.cohesion_weight);
    }
    auto velocity =
        before[self].velocity + acceleration * static_cast<double>(dt);
    const double speed = glm::length(velocity);
    if (p.max_speed <= 0)
      velocity = glm::dvec3{0};
    else if (speed > p.max_speed)
      velocity *= static_cast<double>(p.max_speed) / speed;
    auto position = before[self].position + velocity * static_cast<double>(dt);
    for (int axis = 0; axis < 3; ++axis) {
      if (position[axis] < -config.boundary_half_extent) {
        position[axis] = -config.boundary_half_extent;
        velocity[axis] = std::abs(velocity[axis]);
      } else if (position[axis] > config.boundary_half_extent) {
        position[axis] = config.boundary_half_extent;
        velocity[axis] = -std::abs(velocity[axis]);
      }
    }
    after[self].position = position;
    after[self].velocity = velocity;
    after[self].acceleration = acceleration;
  }
  return after;
}

TEST(ArchetypeReferenceTest,
     EveryStepMatchesIndependentEquationsDuringDummyChurn) {
  constexpr float kDt = 0.03F;
  for (std::uint32_t seed : {0U, 42U, 12345U}) {
    for (std::uint32_t percent : {0U, 40U, 100U}) {
      for (std::size_t count : {1U, 4U, 12U}) {
        SCOPED_TRACE(::testing::Message()
                     << seed << ", " << percent << ", " << count);
        boids::BoidSimulationConfig config;
        config.boid_count = count;
        config.dummy_count = count + 3U;
        config.dummy_change_percent = percent;
        config.random_seed = seed;
        config.spawn_half_extent = 1.5F;
        config.boundary_half_extent = 2.0F;
        config.initial_speed = 9.0F;
        config.parameters.max_speed = 3.0F;
        const auto input = boids::PrepareSimulationInput(config, 60U);
        boids::archetype::BoidSimulation simulation(config);
        simulation.Initialize(input);
        engine::measurement::TimingRecorder disabled;
        disabled.set_enabled(false);
        for (std::size_t step = 0; step < input.dummy_steps.size(); ++step) {
          SCOPED_TRACE(step);
          // Use actual pre-step state so this checks each transition without
          // accumulating differences between float and double trajectories.
          const auto expected =
              ReferenceStep(Snapshot(simulation, input), config, kDt);
          simulation.Update(kDt, input.dummy_steps[step], disabled);
          const auto actual = Snapshot(simulation, input);
          ASSERT_EQ(actual.size(), expected.size());
          for (std::size_t row = 0; row < actual.size(); ++row) {
            SCOPED_TRACE(row);
            for (int axis = 0; axis < 3; ++axis) {
              EXPECT_NEAR(actual[row].position[axis],
                          expected[row].position[axis], 1e-5);
              EXPECT_NEAR(actual[row].velocity[axis],
                          expected[row].velocity[axis], 1e-5);
              EXPECT_NEAR(actual[row].acceleration[axis],
                          expected[row].acceleration[axis], 1e-5);
            }
            EXPECT_NEAR(glm::length(actual[row].rotation), 1.0F, 1e-5F);
            const auto forward = actual[row].rotation * glm::vec3{0, 0, 1};
            const auto direction = glm::dot(expected[row].velocity,
                                            expected[row].velocity) <= 1e-8F
                                       ? glm::dvec3{0, 0, 1}
                                       : glm::normalize(expected[row].velocity);
            for (int axis = 0; axis < 3; ++axis)
              EXPECT_NEAR(forward[axis], direction[axis], 1e-5);
          }
          ASSERT_FALSE(testing::Test::HasFailure());
        }
      }
    }
  }
}

}  // namespace
