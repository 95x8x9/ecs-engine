#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <stdexcept>
#include <vector>

#include "Boids/Archetype/boid_simulation.h"
#include "Boids/Common/boid_simulation_config.h"
#include "Boids/ECS/Components/acceleration.h"
#include "Boids/ECS/Components/velocity.h"
#include "Boids/ECS/Systems/boid_system.h"
#include "Boids/ECS/Systems/movement_system.h"
#include "Boids/ECS/boid_simulation.h"
#include "Boids/OOP/boid_simulation.h"
#include "Engine/Renderer/transform.h"

namespace {

constexpr float kTolerance = 0.00001F;
using boids::ecs::Acceleration;
using boids::ecs::Velocity;
using engine::renderer::Transform;

void ExpectVec3Near(const glm::vec3& actual, const glm::vec3& expected) {
  for (int axis = 0; axis < 3; ++axis) {
    SCOPED_TRACE(axis);
    EXPECT_NEAR(actual[axis], expected[axis], kTolerance);
  }
}

void ExpectRotationNear(const glm::quat& actual, const glm::quat& expected) {
  EXPECT_NEAR(glm::dot(actual, actual), 1.0F, kTolerance);
  EXPECT_NEAR(glm::dot(expected, expected), 1.0F, kTolerance);
  // q and -q represent the same rotation.
  const glm::quat aligned =
      glm::dot(actual, expected) < 0.0F ? -actual : actual;
  for (int component = 0; component < 4; ++component) {
    EXPECT_NEAR(aligned[component], expected[component], kTolerance);
  }
}

void CompareSimulations(const boids::BoidSimulationConfig& config,
                        float delta_seconds = 1.0F / 60.0F) {
  boids::ecs::BoidSimulation ecs_simulation(config);
  boids::oop::BoidSimulation oop_simulation(config);
  ecs_simulation.Initialize();
  oop_simulation.Initialize();

  auto& registry = ecs_simulation.GetRegistry();
  std::vector<engine::ecs::Entity> entities;
  registry.CreateView<Transform, Velocity, Acceleration>().Each(
      [&](engine::ecs::Entity entity, const Transform&, const Velocity&,
          const Acceleration&) { entities.push_back(entity); });
  // A fresh registry allocates IDs in creation order, matching OOP indices.
  std::sort(entities.begin(), entities.end(),
            [](auto left, auto right) { return left.Value() < right.Value(); });
  ASSERT_EQ(entities.size(), config.boid_count);
  ASSERT_EQ(oop_simulation.Boids().size(), config.boid_count);

  for (int step = 0; step <= 100; ++step) {
    SCOPED_TRACE(::testing::Message() << "step=" << step);
    if (step > 0) {
      ecs_simulation.Update(delta_seconds);
      oop_simulation.Update(delta_seconds);
    }
    for (std::size_t index = 0; index < entities.size(); ++index) {
      SCOPED_TRACE(::testing::Message() << "boid=" << index);
      const auto entity = entities[index];
      const auto& object = oop_simulation.Boids()[index];
      const auto* transform = registry.FindComponent<Transform>(entity);
      ASSERT_NE(transform, nullptr);
      {
        SCOPED_TRACE("position");
        ExpectVec3Near(object.Position(), transform->position);
      }
      {
        SCOPED_TRACE("velocity");
        ExpectVec3Near(object.Velocity(),
                       registry.FindComponent<Velocity>(entity)->value);
      }
      {
        SCOPED_TRACE("acceleration");
        ExpectVec3Near(object.Acceleration(),
                       registry.FindComponent<Acceleration>(entity)->value);
      }
      ExpectRotationNear(object.GetTransform().rotation, transform->rotation);
      EXPECT_FLOAT_EQ(object.GetTransform().scale, transform->scale);
    }
    if (::testing::Test::HasFailure()) {
      return;
    }
  }
}

TEST(BoidEquivalenceTest, MatchesInitialStateAndEveryStepForMultipleSeeds) {
  for (std::uint32_t seed : {0U, 42U, 12345U}) {
    SCOPED_TRACE(::testing::Message() << "seed=" << seed);
    boids::BoidSimulationConfig config;
    config.boid_count = 24U;
    config.random_seed = seed;
    CompareSimulations(config);
  }
}

TEST(BoidEquivalenceTest, MatchesWithSpeedLimitingAndBoundaryReflections) {
  boids::BoidSimulationConfig config;
  config.boid_count = 12U;
  config.spawn_half_extent = 1.0F;
  config.boundary_half_extent = 1.0F;
  config.initial_speed = 10.0F;
  config.parameters.max_speed = 3.0F;
  CompareSimulations(config, 0.05F);
}

TEST(BoidEquivalenceTest, MatchesWithoutNeighbors) {
  boids::BoidSimulationConfig config;
  config.boid_count = 1U;
  CompareSimulations(config);
}

TEST(BoidEquivalenceTest, MatchesWithCoincidentPositions) {
  boids::BoidSimulationConfig config;
  config.boid_count = 8U;
  config.spawn_half_extent = 0.0F;
  CompareSimulations(config);
}

TEST(BoidEquivalenceTest, MatchesWithZeroSpeed) {
  boids::BoidSimulationConfig config;
  config.boid_count = 8U;
  config.initial_speed = 0.0F;
  config.parameters.max_speed = 0.0F;
  CompareSimulations(config);
}

TEST(BoidEquivalenceTest, MatchesWithZeroDeltaTime) {
  boids::BoidSimulationConfig config;
  config.boid_count = 8U;
  CompareSimulations(config, 0.0F);
}

TEST(BoidEquivalenceTest, MatchesWithLargeFiniteInitialSpeed) {
  boids::BoidSimulationConfig config;
  config.boid_count = 8U;
  config.initial_speed = 1e20F;
  CompareSimulations(config);
}

// The legacy OOP/ECS simulations do not yet accept common input. Exercise
// their object/system APIs with the same explicit Boid states instead.
void ExpectArithmeticOverflow(const boids::BoidSimulationConfig& config,
                              const boids::SimulationInput& input, float dt) {
  boids::archetype::BoidSimulation archetype(config);
  archetype.Initialize(input);
  engine::measurement::TimingRecorder timing(
      {"entity_changes", "query_preparation", "neighbor_calculation",
       "state_update"});
  timing.begin_measurement(1U);
  timing.begin_step_measurement();
  EXPECT_THROW(archetype.Update(dt, {}, timing), std::overflow_error);
  EXPECT_THROW(timing.end_step_measurement(), std::logic_error);
  EXPECT_THROW(static_cast<void>(timing.result()), std::logic_error);
  engine::measurement::TimingRecorder disabled;
  disabled.set_enabled(false);
  EXPECT_THROW(archetype.Update(dt, {}, disabled), std::logic_error);

  std::vector<boids::oop::BoidObject> objects;
  engine::ecs::Registry registry;
  for (const auto& initial : input.initial_objects) {
    ASSERT_EQ(initial.kind, boids::ObjectKind::Boid);
    objects.emplace_back(initial.position, initial.velocity, config.parameters);
    const auto entity = registry.Create();
    registry.Emplace<Transform>(entity,
                                Transform{.position = initial.position});
    registry.Emplace<Velocity>(entity, Velocity{initial.velocity});
    registry.Emplace<Acceleration>(entity, Acceleration{});
    registry.Emplace<boids::BoidParameters>(entity, config.parameters);
  }
  EXPECT_THROW(
      {
        for (auto& object : objects) object.CalculateAcceleration(objects);
        for (auto& object : objects) object.Move(dt);
      },
      std::overflow_error);
  boids::ecs::BoidSystem boid_system;
  boids::ecs::MovementSystem movement;
  EXPECT_THROW(
      {
        boid_system.Update(registry, dt);
        movement.Update(registry, dt);
      },
      std::overflow_error);
}

TEST(BoidEquivalenceTest, RejectsPositionOverflowBeforeBoundaryClamping) {
  boids::BoidSimulationConfig config;
  config.boid_count = 1U;
  config.boundary_half_extent = 3.4e38F;
  config.parameters.max_speed = 1e38F;
  const boids::SimulationInput input{
      .initial_objects =
          {{boids::ObjectKind::Boid, 0U, {1e38F, 0, 0}, {-1e38F, 0, 0}}},
      .dummy_steps = {}};
  // The exact sum would still be inside the boundary, but the intermediate
  // float product overflows. Reject it instead of inventing a collision.
  ExpectArithmeticOverflow(config, input, 4.0F);
}

TEST(BoidEquivalenceTest, RejectsVelocityOverflowEvenWithZeroSpeedLimit) {
  boids::BoidSimulationConfig config;
  config.boid_count = 2U;
  config.parameters.cohesion_weight = 1e38F;
  config.parameters.max_speed = 0.0F;
  const boids::SimulationInput input{
      .initial_objects = {{boids::ObjectKind::Boid, 0U, {0, 0, 0}, {0, 0, 0}},
                          {boids::ObjectKind::Boid, 1U, {2, 0, 0}, {0, 0, 0}}},
      .dummy_steps = {}};
  ExpectArithmeticOverflow(config, input, 2.0F);
}

TEST(BoidEquivalenceTest, RejectsNeighborOverflowEvenWithZeroAlignmentForce) {
  boids::BoidSimulationConfig config;
  config.boid_count = 3U;
  config.parameters.max_alignment_force = 0.0F;
  config.parameters.separation_weight = 0.0F;
  config.parameters.cohesion_weight = 0.0F;
  const boids::SimulationInput input{
      .initial_objects =
          {{boids::ObjectKind::Boid, 0U, {0, 0, 0}, {2e38F, 0, 0}},
           {boids::ObjectKind::Boid, 1U, {1, 0, 0}, {2e38F, 0, 0}},
           {boids::ObjectKind::Boid, 2U, {2, 0, 0}, {2e38F, 0, 0}}},
      .dummy_steps = {}};
  ExpectArithmeticOverflow(config, input, 0.01F);
}

TEST(BoidEquivalenceTest, LegacySimulationsRejectInvalidConfigAndDt) {
  for (const float invalid : {-1.0F, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
    boids::BoidSimulationConfig config;
    config.initial_speed = invalid;
    EXPECT_THROW(static_cast<void>(boids::ecs::BoidSimulation{config}),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(boids::oop::BoidSimulation{config}),
                 std::invalid_argument);
    config = {};
    config.parameters.max_speed = invalid;
    EXPECT_THROW(static_cast<void>(boids::ecs::BoidSimulation{config}),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(boids::oop::BoidSimulation{config}),
                 std::invalid_argument);
    config = {};
    config.boid_count = 1U;
    boids::ecs::BoidSimulation ecs(config);
    boids::oop::BoidSimulation oop(config);
    ecs.Initialize();
    oop.Initialize();
    const auto before = oop.Boids()[0].Position();
    EXPECT_THROW(ecs.Update(invalid), std::invalid_argument);
    EXPECT_THROW(oop.Update(invalid), std::invalid_argument);
    ExpectVec3Near(oop.Boids()[0].Position(), before);
    EXPECT_NO_THROW(ecs.Update(0.01F));
    EXPECT_NO_THROW(oop.Update(0.01F));
  }
}

}  // namespace
