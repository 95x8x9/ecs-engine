#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "Boids/Archetype/boid_simulation.h"
#include "Boids/ECS/Components/acceleration.h"
#include "Boids/ECS/Components/velocity.h"
#include "Engine/Measurement/timing_recorder.h"
#include "Engine/Renderer/transform.h"

namespace {

using boids::BoidParameters;
using boids::BoidSimulationConfig;
using boids::ObjectKind;
using boids::SimulationInput;
using boids::archetype::BoidSimulation;
using boids::ecs::Acceleration;
using boids::ecs::Velocity;
using engine::ecs::Entity;
using engine::ecs::archetype::Table;
using engine::measurement::StorageEntry;
using engine::measurement::StorageReport;
using engine::measurement::TimingRecorder;
using engine::renderer::Transform;

constexpr float kTolerance = 0.00001F;

void ExpectVector(const glm::vec3& actual, const glm::vec3& expected) {
  for (int axis = 0; axis < 3; ++axis) {
    EXPECT_NEAR(actual[axis], expected[axis], kTolerance);
  }
}

void ExpectRotation(const glm::quat& actual, const glm::vec3& velocity) {
  const auto expected = engine::renderer::RotationFromDirection(velocity);
  const auto aligned = glm::dot(actual, expected) < 0.0F ? -actual : actual;
  for (int component = 0; component < 4; ++component) {
    EXPECT_NEAR(aligned[component], expected[component], kTolerance);
  }
  EXPECT_NEAR(glm::length(actual), 1.0F, kTolerance);
}

const Table& BoidTable(const BoidSimulation& simulation) {
  const auto* table =
      simulation.GetRegistry()
          .FindTable<Transform, Velocity, Acceleration, BoidParameters>();
  if (!table) throw std::logic_error("Missing Boid table");
  return *table;
}

const Table& DummyTable(const BoidSimulation& simulation) {
  const auto* table = simulation.GetRegistry().FindTable<Transform, Velocity>();
  if (!table) throw std::logic_error("Missing Dummy table");
  return *table;
}

std::size_t RowFor(const Table& table, Entity entity) {
  const auto entities = table.Entities();
  const auto found = std::find(entities.begin(), entities.end(), entity);
  if (found == entities.end())
    throw std::logic_error("Entity missing from table");
  return static_cast<std::size_t>(found - entities.begin());
}

struct State {
  Transform transform;
  glm::vec3 velocity;
  glm::vec3 acceleration;
};

State BoidState(const BoidSimulation& simulation, std::size_t logical_id) {
  const auto& table = BoidTable(simulation);
  const auto row = RowFor(table, simulation.EntityFor(logical_id));
  return {table.Column<Transform>()[row], table.Column<Velocity>()[row].value,
          table.Column<Acceleration>()[row].value};
}

void ExpectSameBoids(const BoidSimulation& actual,
                     const BoidSimulation& expected,
                     const SimulationInput& input) {
  for (const auto& initial : input.initial_objects) {
    if (initial.kind != ObjectKind::Boid) continue;
    SCOPED_TRACE(initial.logical_id);
    const auto first = BoidState(actual, initial.logical_id);
    const auto second = BoidState(expected, initial.logical_id);
    ExpectVector(first.transform.position, second.transform.position);
    ExpectVector(first.velocity, second.velocity);
    ExpectVector(first.acceleration, second.acceleration);
    const auto aligned =
        glm::dot(first.transform.rotation, second.transform.rotation) < 0.0F
            ? -first.transform.rotation
            : first.transform.rotation;
    for (int component = 0; component < 4; ++component) {
      EXPECT_NEAR(aligned[component], second.transform.rotation[component],
                  kTolerance);
    }
    EXPECT_FLOAT_EQ(first.transform.scale, second.transform.scale);
  }
}

void ExpectDummiesUnchanged(const BoidSimulation& simulation,
                            const SimulationInput& input) {
  const auto& table = DummyTable(simulation);
  for (const auto& initial : input.initial_objects) {
    if (initial.kind != ObjectKind::Dummy) continue;
    SCOPED_TRACE(initial.logical_id);
    const auto row = RowFor(table, simulation.EntityFor(initial.logical_id));
    const auto& transform = table.Column<Transform>()[row];
    ExpectVector(transform.position, initial.position);
    ExpectVector(table.Column<Velocity>()[row].value, initial.velocity);
    ExpectRotation(transform.rotation, initial.velocity);
    EXPECT_FLOAT_EQ(transform.scale, 0.7F);
  }
}

void Step(BoidSimulation& simulation, float dt,
          std::span<const std::size_t> ids = {}) {
  TimingRecorder timing;
  timing.set_enabled(false);
  simulation.Update(dt, ids, timing);
}

const StorageEntry& Entry(const StorageReport& report,
                          const std::string& name) {
  for (const auto& entry : report.entries()) {
    if (entry.name == name) return entry;
  }
  throw std::logic_error("Missing storage entry: " + name);
}

TEST(ArchetypeBoidSimulationTest, CreatesSeparateTablesInCommonInputOrder) {
  BoidSimulationConfig config;
  config.boid_count = 3U;
  config.dummy_count = 5U;
  auto input = boids::PrepareSimulationInput(config, 2U);
  input.initial_objects[0].velocity = glm::vec3{0.0F};
  BoidSimulation simulation(config);
  simulation.Initialize(input);

  const auto& boids = BoidTable(simulation);
  const auto& dummies = DummyTable(simulation);
  EXPECT_EQ(boids.Size(), config.boid_count);
  EXPECT_EQ(dummies.Size(), config.dummy_count);
  EXPECT_EQ(simulation.GetRegistry().EntityCount(),
            input.initial_objects.size());
  EXPECT_EQ((simulation.GetRegistry()
                 .FindTable<Transform, Velocity, BoidParameters>()),
            nullptr);
  EXPECT_THROW(static_cast<void>(dummies.Column<BoidParameters>()),
               std::logic_error);
  EXPECT_THROW(static_cast<void>(dummies.Column<Acceleration>()),
               std::logic_error);
  std::size_t boid_row = 0U;
  std::size_t dummy_row = 0U;
  for (const auto& initial : input.initial_objects) {
    const auto entity = simulation.EntityFor(initial.logical_id);
    EXPECT_TRUE(simulation.GetRegistry().IsAlive(entity));
    // Fresh Registry IDs expose the order of cross-table Create calls.
    EXPECT_EQ(entity.Value(), initial.logical_id);
    const auto& table = initial.kind == ObjectKind::Boid ? boids : dummies;
    const auto row =
        initial.kind == ObjectKind::Boid ? boid_row++ : dummy_row++;
    EXPECT_EQ(table.Entities()[row], entity);
    ExpectVector(table.Column<Transform>()[row].position, initial.position);
    ExpectVector(table.Column<Velocity>()[row].value, initial.velocity);
    ExpectRotation(table.Column<Transform>()[row].rotation, initial.velocity);
    EXPECT_FLOAT_EQ(table.Column<Transform>()[row].scale, 0.7F);
    if (initial.kind == ObjectKind::Boid) {
      ExpectVector(table.Column<Acceleration>()[row].value, glm::vec3{0.0F});
      const auto& parameters = table.Column<BoidParameters>()[row];
      EXPECT_FLOAT_EQ(parameters.neighbor_radius,
                      config.parameters.neighbor_radius);
      EXPECT_FLOAT_EQ(parameters.separation_radius,
                      config.parameters.separation_radius);
      EXPECT_FLOAT_EQ(parameters.separation_weight,
                      config.parameters.separation_weight);
      EXPECT_FLOAT_EQ(parameters.alignment_weight,
                      config.parameters.alignment_weight);
      EXPECT_FLOAT_EQ(parameters.cohesion_weight,
                      config.parameters.cohesion_weight);
      EXPECT_FLOAT_EQ(parameters.preferred_speed,
                      config.parameters.preferred_speed);
      EXPECT_FLOAT_EQ(parameters.max_speed, config.parameters.max_speed);
      EXPECT_FLOAT_EQ(parameters.max_alignment_force,
                      config.parameters.max_alignment_force);
    }
  }
}

TEST(ArchetypeBoidSimulationTest, RejectsMismatchedAndNonfiniteInput) {
  BoidSimulationConfig config;
  config.boid_count = 2U;
  config.dummy_count = 2U;
  const auto valid = boids::PrepareSimulationInput(config, 0U);
  BoidSimulation simulation(config);
  auto input = valid;
  input.initial_objects.pop_back();
  EXPECT_THROW(simulation.Initialize(input), std::invalid_argument);
  input = valid;
  input.initial_objects[0].logical_id = 1U;
  EXPECT_THROW(simulation.Initialize(input), std::invalid_argument);
  input = valid;
  input.initial_objects[0].kind = ObjectKind::Dummy;
  EXPECT_THROW(simulation.Initialize(input), std::invalid_argument);
  input = valid;
  input.initial_objects[0].kind = static_cast<ObjectKind>(99);
  EXPECT_THROW(simulation.Initialize(input), std::invalid_argument);
  input = valid;
  input.initial_objects[0].position.x = std::numeric_limits<float>::infinity();
  EXPECT_THROW(simulation.Initialize(input), std::invalid_argument);
  input = valid;
  input.initial_objects[0].velocity.y = std::numeric_limits<float>::quiet_NaN();
  EXPECT_THROW(simulation.Initialize(input), std::invalid_argument);
  EXPECT_EQ(simulation.GetRegistry().EntityCount(), 0U);
  simulation.Initialize(valid);
  EXPECT_EQ(simulation.GetRegistry().EntityCount(),
            valid.initial_objects.size());
}

TEST(ArchetypeBoidSimulationTest, ValidatesConfigWhenBorrowingPreparedInput) {
  for (const float invalid : {-1.0F, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
    BoidSimulationConfig config;
    config.boundary_half_extent = invalid;
    EXPECT_THROW(static_cast<void>(BoidSimulation{config}),
                 std::invalid_argument);
    config = {};
    config.spawn_half_extent = invalid;
    EXPECT_THROW(static_cast<void>(BoidSimulation{config}),
                 std::invalid_argument);
    config = {};
    config.initial_speed = invalid;
    EXPECT_THROW(static_cast<void>(BoidSimulation{config}),
                 std::invalid_argument);
    for (auto member :
         {&BoidParameters::neighbor_radius, &BoidParameters::separation_radius,
          &BoidParameters::separation_weight, &BoidParameters::alignment_weight,
          &BoidParameters::cohesion_weight, &BoidParameters::preferred_speed,
          &BoidParameters::max_speed, &BoidParameters::max_alignment_force}) {
      config = {};
      config.parameters.*member = invalid;
      EXPECT_THROW(static_cast<void>(BoidSimulation{config}),
                   std::invalid_argument);
    }
  }
  BoidSimulationConfig config;
  config.boundary_half_extent = 0.0F;
  EXPECT_THROW(static_cast<void>(BoidSimulation{config}),
               std::invalid_argument);
  config = {};
  config.spawn_half_extent = config.boundary_half_extent + 1.0F;
  EXPECT_THROW(static_cast<void>(BoidSimulation{config}),
               std::invalid_argument);
  config = {};
  config.dummy_change_percent = 101U;
  EXPECT_THROW(static_cast<void>(BoidSimulation{config}),
               std::invalid_argument);
  config = {};
  config.boid_count = std::numeric_limits<std::size_t>::max();
  config.dummy_count = 1U;
  EXPECT_THROW(static_cast<void>(BoidSimulation{config}), std::overflow_error);
  config = {};
  config.boid_count = static_cast<std::size_t>(Entity::kInvalidId) + 1U;
  EXPECT_THROW(static_cast<void>(BoidSimulation{config}), std::overflow_error);
}

TEST(ArchetypeBoidSimulationTest,
     RequiresInitializationAndFiniteNonnegativeDt) {
  BoidSimulationConfig config;
  config.boid_count = 1U;
  const auto input = boids::PrepareSimulationInput(config, 0U);
  BoidSimulation simulation(config);
  EXPECT_THROW(Step(simulation, 0.01F), std::logic_error);
  EXPECT_THROW(static_cast<void>(simulation.EntityFor(0U)), std::out_of_range);
  simulation.Initialize(input);
  EXPECT_THROW(static_cast<void>(simulation.EntityFor(1U)), std::out_of_range);
  EXPECT_THROW(static_cast<void>(simulation.EntityFor(
                   std::numeric_limits<std::size_t>::max())),
               std::out_of_range);
  for (const float invalid : {-0.01F, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
    EXPECT_THROW(Step(simulation, invalid), std::invalid_argument);
  }
  ExpectVector(BoidState(simulation, 0U).transform.position,
               input.initial_objects[0].position);
  EXPECT_NO_THROW(Step(simulation, 0.0F));
}

struct ChurnCase {
  std::size_t boids;
  std::size_t dummies;
  std::uint32_t percent;
};

class ArchetypeChurnTest : public testing::TestWithParam<ChurnCase> {};

TEST_P(ArchetypeChurnTest, AppliesConsecutiveWarmupAndMeasuredPlansHeadlessly) {
  const auto scenario = GetParam();
  BoidSimulationConfig config;
  config.boid_count = scenario.boids;
  config.dummy_count = scenario.dummies;
  config.dummy_change_percent = scenario.percent;
  constexpr std::size_t kWarmup = 3U;
  constexpr std::size_t kMeasured = 9U;
  const auto input = boids::PrepareSimulationInput(config, kWarmup + kMeasured);
  BoidSimulation simulation(config);
  TimingRecorder initialization({"initialization"});
  initialization.start("initialization");
  simulation.Initialize(input);
  initialization.stop("initialization");
  EXPECT_GE(initialization.total_ms("initialization"), 0.0);
  TimingRecorder timing({"entity_changes", "query_preparation",
                         "neighbor_calculation", "state_update", "unmeasured"});
  timing.set_enabled(false);
  for (std::size_t step = 0; step < input.dummy_steps.size(); ++step) {
    SCOPED_TRACE(step);
    if (step == kWarmup) timing.begin_measurement(kMeasured);
    std::vector<Entity> previous;
    for (std::size_t id = 0; id < input.initial_objects.size(); ++id) {
      previous.push_back(simulation.EntityFor(id));
    }
    const auto& plan = input.dummy_steps[step];
    EXPECT_EQ(plan.size(), scenario.dummies * scenario.percent / 100U);
    if (step >= kWarmup) timing.begin_step_measurement();
    simulation.Update(1.0F / 60.0F, plan, timing);
    if (step >= kWarmup) timing.end_step_measurement();
    EXPECT_EQ(simulation.GetRegistry().EntityCount(),
              scenario.boids + scenario.dummies);
    EXPECT_EQ(BoidTable(simulation).Size(), scenario.boids);
    EXPECT_EQ(DummyTable(simulation).Size(), scenario.dummies);
    // The Registry reuses free IDs in reverse order. This verifies that all
    // deletions precede all recreations, which follow the supplied plan order.
    for (std::size_t index = 0; index < plan.size(); ++index) {
      EXPECT_EQ(simulation.EntityFor(plan[index]).Value(),
                previous[plan[plan.size() - index - 1U]].Value());
    }
    for (std::size_t id = 0; id < previous.size(); ++id) {
      const bool replaced =
          std::find(plan.begin(), plan.end(), id) != plan.end();
      EXPECT_EQ(simulation.GetRegistry().IsAlive(previous[id]), !replaced);
      EXPECT_TRUE(simulation.GetRegistry().IsAlive(simulation.EntityFor(id)));
      if (replaced) {
        EXPECT_NE(simulation.EntityFor(id), previous[id]);
      } else {
        EXPECT_EQ(simulation.EntityFor(id), previous[id]);
      }
    }
    ExpectDummiesUnchanged(simulation, input);
  }
  const auto result = timing.result();
  EXPECT_EQ(result.measured_steps, kMeasured);
  ASSERT_EQ(result.sections.size(), 5U);
  for (const auto& section : result.sections) {
    const bool measured = section.name != "unmeasured";
    EXPECT_EQ(section.recorded_steps, measured ? kMeasured : 0U);
    EXPECT_EQ(section.mean_ms_per_step.has_value(), measured);
    for (const auto& sample : section.samples) {
      EXPECT_EQ(sample.recorded, measured);
      bool zero = section.name == "entity_changes" &&
                  scenario.dummies * scenario.percent / 100U == 0U;
      zero |= scenario.boids == 0U && (section.name == "neighbor_calculation" ||
                                       section.name == "state_update");
      if (zero) {
        EXPECT_DOUBLE_EQ(sample.elapsed_ms, 0.0);
        EXPECT_EQ(sample.calls, 0U);
      } else if (measured) {
        EXPECT_EQ(sample.calls, 1U);
        EXPECT_GE(sample.elapsed_ms, 0.0);
      }
    }
  }
  EXPECT_TRUE(simulation.GetStorageReport().complete());
}

INSTANTIATE_TEST_SUITE_P(
    CountsAndPercentages, ArchetypeChurnTest,
    testing::Values(ChurnCase{0U, 0U, 0U}, ChurnCase{4U, 0U, 100U},
                    ChurnCase{0U, 5U, 100U}, ChurnCase{0U, 7U, 40U},
                    ChurnCase{3U, 5U, 0U}, ChurnCase{3U, 7U, 40U},
                    ChurnCase{3U, 5U, 100U}, ChurnCase{3U, 1U, 1U},
                    ChurnCase{3U, 5U, 20U}));

TEST(ArchetypeBoidSimulationTest,
     ReinitializesMappingAndInvalidatesOldEntities) {
  BoidSimulationConfig config;
  config.boid_count = 3U;
  config.dummy_count = 4U;
  config.dummy_change_percent = 100U;
  const auto first = boids::PrepareSimulationInput(config, 1U);
  BoidSimulation simulation(config);
  simulation.Initialize(first);
  Step(simulation, 0.1F, first.dummy_steps[0]);
  std::vector<Entity> previous;
  for (std::size_t id = 0; id < first.initial_objects.size(); ++id) {
    previous.push_back(simulation.EntityFor(id));
  }
  config.random_seed += 1U;
  const auto second = boids::PrepareSimulationInput(config, 1U);
  simulation.Initialize(second);
  bool id_differs_from_logical_id = false;
  for (const auto& initial : second.initial_objects) {
    EXPECT_FALSE(
        simulation.GetRegistry().IsAlive(previous[initial.logical_id]));
    const auto entity = simulation.EntityFor(initial.logical_id);
    EXPECT_TRUE(simulation.GetRegistry().IsAlive(entity));
    id_differs_from_logical_id |= entity.Value() != initial.logical_id;
    if (initial.kind == ObjectKind::Boid) {
      const auto state = BoidState(simulation, initial.logical_id);
      ExpectVector(state.transform.position, initial.position);
      ExpectVector(state.velocity, initial.velocity);
      ExpectVector(state.acceleration, glm::vec3{0.0F});
    }
  }
  EXPECT_TRUE(id_differs_from_logical_id);
  ExpectDummiesUnchanged(simulation, second);
  Step(simulation, 0.0F, second.dummy_steps[0]);
  ExpectDummiesUnchanged(simulation, second);
}

TEST(ArchetypeBoidSimulationTest, DummyChurnPreservesBoidStatesByLogicalId) {
  BoidSimulationConfig config;
  config.boid_count = 16U;
  config.dummy_count = 23U;
  config.spawn_half_extent = 2.0F;
  config.boundary_half_extent = 3.0F;
  constexpr std::size_t kWarmup = 7U;
  constexpr std::size_t kMeasured = 73U;
  const auto baseline_input =
      boids::PrepareSimulationInput(config, kWarmup + kMeasured);
  for (const std::uint32_t percent : {0U, 40U, 100U}) {
    SCOPED_TRACE(percent);
    config.dummy_change_percent = percent;
    const auto changing_input =
        boids::PrepareSimulationInput(config, kWarmup + kMeasured);
    BoidSimulation baseline(config);
    BoidSimulation changing(config);
    baseline.Initialize(baseline_input);
    changing.Initialize(changing_input);
    for (std::size_t step = 0; step < kWarmup + kMeasured; ++step) {
      SCOPED_TRACE(step);
      Step(baseline, 1.0F / 60.0F, baseline_input.dummy_steps[step]);
      Step(changing, 1.0F / 60.0F, changing_input.dummy_steps[step]);
      ExpectSameBoids(changing, baseline, baseline_input);
      for (const auto& transform : BoidTable(changing).Column<Transform>()) {
        for (int axis = 0; axis < 3; ++axis) {
          EXPECT_TRUE(std::isfinite(transform.position[axis]));
          EXPECT_LE(std::abs(transform.position[axis]),
                    config.boundary_half_extent);
        }
      }
    }
  }
}

TEST(ArchetypeBoidSimulationTest,
     MeasurementAfterWarmupMatchesContinuousUninstrumentedExecution) {
  constexpr std::size_t kWarmup = 5U;
  constexpr std::size_t kMeasured = 25U;
  constexpr float kDt = 1.0F / 60.0F;
  for (const std::uint32_t percent : {0U, 40U, 100U}) {
    SCOPED_TRACE(percent);
    BoidSimulationConfig config;
    config.boid_count = 12U;
    config.dummy_count = 17U;
    config.dummy_change_percent = percent;
    config.spawn_half_extent = 1.0F;
    config.boundary_half_extent = 2.0F;
    const auto input =
        boids::PrepareSimulationInput(config, kWarmup + kMeasured);
    BoidSimulation continuous(config);
    BoidSimulation measured(config);
    continuous.Initialize(input);
    measured.Initialize(input);
    TimingRecorder disabled;
    disabled.set_enabled(false);
    TimingRecorder timing({"entity_changes", "query_preparation",
                           "neighbor_calculation", "state_update"});
    timing.set_enabled(false);

    for (std::size_t step = 0; step < input.dummy_steps.size(); ++step) {
      SCOPED_TRACE(step);
      if (step == kWarmup) timing.begin_measurement(kMeasured);
      continuous.Update(kDt, input.dummy_steps[step], disabled);
      if (step >= kWarmup) timing.begin_step_measurement();
      measured.Update(kDt, input.dummy_steps[step], timing);
      if (step >= kWarmup) timing.end_step_measurement();
      ExpectSameBoids(measured, continuous, input);
      ExpectDummiesUnchanged(measured, input);
      EXPECT_EQ(measured.GetRegistry().EntityCount(),
                input.initial_objects.size());
      for (const auto& initial : input.initial_objects) {
        // Identical creation/deletion sequences must also preserve the handle
        // generations through the transition; no reinitialization or plan
        // reset.
        EXPECT_EQ(measured.EntityFor(initial.logical_id),
                  continuous.EntityFor(initial.logical_id));
      }
      ASSERT_FALSE(testing::Test::HasFailure());
    }

    const auto result = timing.result();
    EXPECT_EQ(result.measured_steps, kMeasured);
    ASSERT_EQ(result.step_ms.size(), kMeasured);
    ASSERT_EQ(result.sections.size(), 4U);
    for (const auto& section : result.sections) {
      EXPECT_EQ(section.recorded_steps, kMeasured);
      EXPECT_TRUE(section.mean_ms_per_step.has_value());
    }
    const auto expected_storage = continuous.GetStorageReport();
    const auto actual_storage = measured.GetStorageReport();
    EXPECT_EQ(actual_storage.size_bytes(), expected_storage.size_bytes());
    EXPECT_EQ(actual_storage.capacity_bytes(),
              expected_storage.capacity_bytes());
  }
}

TEST(ArchetypeBoidSimulationTest, DummyPositionAndVelocityDoNotAffectBoids) {
  BoidSimulationConfig config;
  config.boid_count = 6U;
  config.dummy_count = 9U;
  config.dummy_change_percent = 35U;
  const auto input = boids::PrepareSimulationInput(config, 30U);
  auto different = input;
  for (auto& initial : different.initial_objects) {
    if (initial.kind == ObjectKind::Dummy) {
      initial.position = input.initial_objects[0].position;
      initial.velocity = {1000.0F, -2000.0F, 3000.0F};
    }
  }
  BoidSimulation first(config);
  BoidSimulation second(config);
  first.Initialize(input);
  second.Initialize(different);
  for (std::size_t step = 0; step < input.dummy_steps.size(); ++step) {
    Step(first, 1.0F / 60.0F, input.dummy_steps[step]);
    Step(second, 1.0F / 60.0F, different.dummy_steps[step]);
    ExpectSameBoids(second, first, input);
    ExpectDummiesUnchanged(first, input);
    ExpectDummiesUnchanged(second, different);
  }
}

TEST(ArchetypeBoidSimulationTest,
     CalculatesAllAccelerationsBeforeUpdatingState) {
  BoidSimulationConfig config;
  config.boid_count = 2U;
  config.dummy_count = 1U;
  config.parameters = {.neighbor_radius = 10.0F,
                       .separation_radius = 3.0F,
                       .separation_weight = 2.0F,
                       .alignment_weight = 3.0F,
                       .cohesion_weight = 4.0F,
                       .preferred_speed = 2.0F,
                       .max_speed = 100.0F,
                       .max_alignment_force = 0.5F};
  const SimulationInput input{
      .initial_objects = {{ObjectKind::Boid, 0U, {0, 0, 0}, {1, 0, 0}},
                          {ObjectKind::Dummy, 1U, {0, 0, 0}, {100, 100, 100}},
                          {ObjectKind::Boid, 2U, {2, 0, 0}, {0, 2, 0}}},
      .dummy_steps = {}};
  BoidSimulation simulation(config);
  simulation.Initialize(input);
  constexpr float kDt = 0.25F;
  Step(simulation, kDt);
  // Alignment alone is limited to 0.5 before its weight of 3 is applied.
  const glm::vec3 first_acceleration =
      glm::vec3{-1, 0, 0} +
      glm::vec3{-1, 2, 0} * (0.5F / std::sqrt(5.0F)) * 3.0F +
      glm::vec3{8, 0, 0};
  const glm::vec3 second_acceleration =
      glm::vec3{1, 0, 0} +
      glm::vec3{2, -2, 0} * (0.5F / std::sqrt(8.0F)) * 3.0F +
      glm::vec3{-8, 0, 0};
  const std::array accelerations{first_acceleration, second_acceleration};
  for (std::size_t row = 0; row < accelerations.size(); ++row) {
    const auto& initial = input.initial_objects[row * 2U];
    const auto state = BoidState(simulation, initial.logical_id);
    ExpectVector(state.acceleration, accelerations[row]);
    const auto velocity = initial.velocity + accelerations[row] * kDt;
    ExpectVector(state.velocity, velocity);
    ExpectVector(state.transform.position, initial.position + velocity * kDt);
    ExpectRotation(state.transform.rotation, velocity);
  }
  ExpectDummiesUnchanged(simulation, input);
}

TEST(ArchetypeBoidSimulationTest,
     LimitsSpeedThenAppliesBoundaryAndOrientation) {
  BoidSimulationConfig config;
  config.boid_count = 1U;
  config.dummy_count = 1U;
  config.boundary_half_extent = 1.0F;
  config.spawn_half_extent = 0.5F;
  config.parameters.max_speed = 2.0F;
  const SimulationInput input{
      .initial_objects = {{ObjectKind::Boid, 0U, {0.9F, -0.9F, 0}, {3, -4, 0}},
                          {ObjectKind::Dummy, 1U, {-10, 10, 5}, {30, -40, 5}}},
      .dummy_steps = {}};
  BoidSimulation simulation(config);
  simulation.Initialize(input);
  Step(simulation, 1.0F);
  const auto state = BoidState(simulation, 0U);
  ExpectVector(state.acceleration, glm::vec3{0.0F});
  ExpectVector(state.transform.position, {1, -1, 0});
  ExpectVector(state.velocity, {-1.2F, 1.6F, 0});
  ExpectRotation(state.transform.rotation, state.velocity);
  ExpectDummiesUnchanged(simulation, input);
}

TEST(ArchetypeBoidSimulationTest, ZeroMaxSpeedStopsBoidAndResetsOrientation) {
  BoidSimulationConfig config;
  config.boid_count = 1U;
  config.parameters.max_speed = 0.0F;
  const auto input = boids::PrepareSimulationInput(config, 0U);
  BoidSimulation simulation(config);
  simulation.Initialize(input);
  Step(simulation, 1.0F);
  const auto state = BoidState(simulation, 0U);
  ExpectVector(state.transform.position, input.initial_objects[0].position);
  ExpectVector(state.velocity, glm::vec3{0.0F});
  ExpectRotation(state.transform.rotation, glm::vec3{0.0F});
}

TEST(ArchetypeBoidSimulationTest, ZeroDtComputesAccelerationWithoutMoving) {
  BoidSimulationConfig config;
  config.boid_count = 2U;
  const SimulationInput input{
      .initial_objects = {{ObjectKind::Boid, 0U, {0, 0, 0}, {1, 0, 0}},
                          {ObjectKind::Boid, 1U, {1, 0, 0}, {0, 1, 0}}},
      .dummy_steps = {}};
  BoidSimulation simulation(config);
  simulation.Initialize(input);
  Step(simulation, 0.0F);
  for (const auto& initial : input.initial_objects) {
    const auto state = BoidState(simulation, initial.logical_id);
    ExpectVector(state.transform.position, initial.position);
    ExpectVector(state.velocity, initial.velocity);
    EXPECT_GT(glm::length(state.acceleration), 0.0F);
  }
}

TEST(ArchetypeBoidSimulationTest, HandlesOverlapZeroVelocityAndRadiusCutoff) {
  BoidSimulationConfig config;
  config.boid_count = 3U;
  config.parameters.neighbor_radius = 3.0F;
  const SimulationInput input{
      .initial_objects = {{ObjectKind::Boid, 0U, {0, 0, 0}, {0, 0, 0}},
                          {ObjectKind::Boid, 1U, {0, 0, 0}, {0, 0, 0}},
                          {ObjectKind::Boid, 2U, {3, 0, 0}, {0, 0, 0}}},
      .dummy_steps = {}};
  BoidSimulation simulation(config);
  simulation.Initialize(input);
  Step(simulation, 1.0F);
  for (const auto& initial : input.initial_objects) {
    const auto state = BoidState(simulation, initial.logical_id);
    ExpectVector(state.acceleration, glm::vec3{0.0F});
    ExpectVector(state.velocity, glm::vec3{0.0F});
    ExpectVector(state.transform.position, initial.position);
    ExpectRotation(state.transform.rotation, glm::vec3{0.0F});
  }
}

TEST(ArchetypeBoidSimulationTest,
     ReportsOwnedStorageOnceAndExcludesBorrowedInput) {
  BoidSimulationConfig config;
  config.boid_count = 3U;
  config.dummy_count = 5U;
  config.dummy_change_percent = 100U;
  const auto input = boids::PrepareSimulationInput(config, 100U);
  BoidSimulation simulation(config);
  simulation.Initialize(input);
  Step(simulation, 0.01F, input.dummy_steps[0]);
  const auto report = simulation.GetStorageReport();
  StorageReport registry;
  simulation.GetRegistry().AppendStorageReport(registry, "registry");
  EXPECT_TRUE(report.complete());
  EXPECT_EQ(report.entries().size(), registry.entries().size() + 2U);
  EXPECT_EQ(Entry(report, "simulation.object").size_bytes,
            sizeof(BoidSimulation));
  const auto& mapping = Entry(report, "entities.logical_ids");
  EXPECT_EQ(mapping.size_bytes, input.initial_objects.size() * sizeof(Entity));
  EXPECT_EQ(report.size_bytes(),
            registry.size_bytes() + sizeof(BoidSimulation) +
                input.initial_objects.size() * sizeof(Entity));
  ASSERT_TRUE(mapping.capacity_bytes.has_value());
  EXPECT_EQ(report.capacity_bytes(), registry.capacity_bytes() +
                                         sizeof(BoidSimulation) +
                                         *mapping.capacity_bytes);
  std::set<std::string> names;
  for (const auto& entry : report.entries()) {
    EXPECT_TRUE(names.insert(entry.name).second);
    EXPECT_FALSE(entry.name.starts_with("input"));
    ASSERT_TRUE(entry.size_bytes.has_value());
    ASSERT_TRUE(entry.capacity_bytes.has_value());
    EXPECT_GE(*entry.capacity_bytes, *entry.size_bytes);
  }
  for (const auto& entry : registry.entries()) {
    EXPECT_EQ(Entry(report, entry.name).size_bytes, entry.size_bytes);
    EXPECT_EQ(Entry(report, entry.name).capacity_bytes, entry.capacity_bytes);
  }
  EXPECT_EQ(Entry(report, "registry.tables.0.entities").size_bytes,
            config.boid_count * sizeof(Entity));
  EXPECT_EQ(Entry(report, "registry.tables.1.entities").size_bytes,
            config.dummy_count * sizeof(Entity));
  const std::string boid_columns = "registry.tables.0.columns.";
  const std::string dummy_columns = "registry.tables.1.columns.";
  EXPECT_EQ(Entry(report, boid_columns + typeid(Transform).name() + ".values")
                .size_bytes,
            config.boid_count * sizeof(Transform));
  EXPECT_EQ(Entry(report, dummy_columns + typeid(Transform).name() + ".values")
                .size_bytes,
            config.dummy_count * sizeof(Transform));
  EXPECT_EQ(Entry(report, boid_columns + typeid(Velocity).name() + ".values")
                .size_bytes,
            config.boid_count * sizeof(Velocity));
  EXPECT_EQ(Entry(report, dummy_columns + typeid(Velocity).name() + ".values")
                .size_bytes,
            config.dummy_count * sizeof(Velocity));
  EXPECT_EQ(
      Entry(report, boid_columns + typeid(Acceleration).name() + ".values")
          .size_bytes,
      config.boid_count * sizeof(Acceleration));
  EXPECT_EQ(
      Entry(report, boid_columns + typeid(BoidParameters).name() + ".values")
          .size_bytes,
      config.boid_count * sizeof(BoidParameters));
}

}  // namespace
