#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <tuple>

#include "Boids/Common/simulation_input.h"

namespace {

using boids::BoidSimulationConfig;
using boids::ObjectKind;
using boids::PrepareSimulationInput;
using boids::SimulationInput;

void ExpectSameInitialObjects(const SimulationInput& first,
                              const SimulationInput& second) {
  ASSERT_EQ(first.initial_objects.size(), second.initial_objects.size());
  for (std::size_t index = 0; index < first.initial_objects.size(); ++index) {
    SCOPED_TRACE(index);
    const auto& left = first.initial_objects[index];
    const auto& right = second.initial_objects[index];
    EXPECT_EQ(left.kind, right.kind);
    EXPECT_EQ(left.logical_id, right.logical_id);
    for (int axis = 0; axis < 3; ++axis) {
      EXPECT_EQ(left.position[axis], right.position[axis]);
      EXPECT_EQ(left.velocity[axis], right.velocity[axis]);
    }
  }
}

std::vector<std::size_t> DummyIds(const SimulationInput& input) {
  std::vector<std::size_t> ids;
  for (const auto& object : input.initial_objects) {
    if (object.kind == ObjectKind::Dummy) {
      ids.push_back(object.logical_id);
    }
  }
  return ids;
}

class SimulationInputOrderTest
    : public testing::TestWithParam<std::tuple<std::size_t, std::size_t>> {};

TEST_P(SimulationInputOrderTest, AlternatesThenAppendsRemainderWithStableIds) {
  const auto [boid_count, dummy_count] = GetParam();
  BoidSimulationConfig config;
  config.boid_count = boid_count;
  config.dummy_count = dummy_count;
  const auto input = PrepareSimulationInput(config, 0U);

  ASSERT_EQ(input.initial_objects.size(), boid_count + dummy_count);
  EXPECT_TRUE(input.dummy_steps.empty());
  std::size_t seen_boids = 0U;
  std::size_t seen_dummies = 0U;
  for (std::size_t id = 0; id < input.initial_objects.size(); ++id) {
    SCOPED_TRACE(id);
    const auto& object = input.initial_objects[id];
    EXPECT_EQ(object.logical_id, id);
    if (seen_boids < boid_count && seen_dummies < dummy_count) {
      EXPECT_EQ(object.kind,
                id % 2U == 0U ? ObjectKind::Boid : ObjectKind::Dummy);
    } else {
      EXPECT_EQ(object.kind, seen_boids == boid_count ? ObjectKind::Dummy
                                                      : ObjectKind::Boid);
    }
    object.kind == ObjectKind::Boid ? ++seen_boids : ++seen_dummies;
  }
  EXPECT_EQ(seen_boids, boid_count);
  EXPECT_EQ(seen_dummies, dummy_count);
}

INSTANTIATE_TEST_SUITE_P(EmptyEqualAndUnequalCounts, SimulationInputOrderTest,
                         testing::Values(std::tuple{0U, 0U}, std::tuple{4U, 0U},
                                         std::tuple{0U, 4U}, std::tuple{1U, 1U},
                                         std::tuple{4U, 4U}, std::tuple{2U, 5U},
                                         std::tuple{5U, 2U}));

TEST(SimulationInputTest, RepeatsBothRandomStreamsForTheSameSeed) {
  BoidSimulationConfig config;
  config.boid_count = 17U;
  config.dummy_count = 23U;
  config.dummy_change_percent = 37U;
  for (const auto seed : {0U, 42U, std::numeric_limits<std::uint32_t>::max()}) {
    SCOPED_TRACE(seed);
    config.random_seed = seed;
    const auto first = PrepareSimulationInput(config, 25U);
    const auto second = PrepareSimulationInput(config, 25U);
    ExpectSameInitialObjects(first, second);
    EXPECT_EQ(first.dummy_steps, second.dummy_steps);
  }
}

TEST(SimulationInputTest, DifferentSeedsChangeInitialStatesAndSelections) {
  BoidSimulationConfig config;
  config.dummy_count = 30U;
  config.dummy_change_percent = 50U;
  const auto first = PrepareSimulationInput(config, 20U);
  ++config.random_seed;
  const auto second = PrepareSimulationInput(config, 20U);
  EXPECT_NE(first.initial_objects.front().position,
            second.initial_objects.front().position);
  EXPECT_NE(first.dummy_steps, second.dummy_steps);
}

TEST(SimulationInputTest, DrawsPositionThenDirectionInXyzOrder) {
  BoidSimulationConfig config;
  config.boid_count = 3U;
  config.dummy_count = 4U;
  const auto input = PrepareSimulationInput(config, 0U);
  std::mt19937 random{config.random_seed};
  std::uniform_real_distribution<float> position(-config.spawn_half_extent,
                                                 config.spawn_half_extent);
  std::uniform_real_distribution<float> direction(-1.0F, 1.0F);
  for (const auto& object : input.initial_objects) {
    SCOPED_TRACE(object.logical_id);
    EXPECT_EQ(object.position.x, position(random));
    EXPECT_EQ(object.position.y, position(random));
    EXPECT_EQ(object.position.z, position(random));
    const float x = direction(random);
    const float y = direction(random);
    const float z = direction(random);
    const float scale = config.initial_speed / std::sqrt(x * x + y * y + z * z);
    EXPECT_NEAR(object.velocity.x, x * scale, 1e-6F);
    EXPECT_NEAR(object.velocity.y, y * scale, 1e-6F);
    EXPECT_NEAR(object.velocity.z, z * scale, 1e-6F);
  }
}

TEST(SimulationInputTest, GeneratesFiniteStatesInsideSpawnRangeAtInitialSpeed) {
  BoidSimulationConfig config;
  config.boid_count = 100U;
  config.dummy_count = 100U;
  config.spawn_half_extent = 2.0F;
  config.initial_speed = 1.5F;
  const auto input = PrepareSimulationInput(config, 0U);
  for (const auto& object : input.initial_objects) {
    for (int axis = 0; axis < 3; ++axis) {
      EXPECT_TRUE(std::isfinite(object.position[axis]));
      EXPECT_GE(object.position[axis], -config.spawn_half_extent);
      EXPECT_LE(object.position[axis], config.spawn_half_extent);
      EXPECT_TRUE(std::isfinite(object.velocity[axis]));
    }
    EXPECT_NEAR(glm::length(object.velocity), config.initial_speed, 1e-6F);
  }
}

TEST(SimulationInputTest, AcceptsZeroSpawnSpeedAndParameters) {
  BoidSimulationConfig config;
  config.dummy_count = 2U;
  config.spawn_half_extent = 0.0F;
  config.initial_speed = 0.0F;
  config.parameters = {};
  const auto input = PrepareSimulationInput(config, 3U);
  for (const auto& object : input.initial_objects) {
    EXPECT_EQ(object.position, glm::vec3(0.0F));
    EXPECT_EQ(object.velocity, glm::vec3(0.0F));
  }
}

class SimulationInputPlanTest
    : public testing::TestWithParam<
          std::tuple<std::size_t, std::size_t, std::uint32_t, std::size_t>> {};

TEST_P(SimulationInputPlanTest, SelectsExactCountOfSortedUniqueDummyIds) {
  const auto [boids, dummies, percent, expected_changes] = GetParam();
  BoidSimulationConfig config;
  config.boid_count = boids;
  config.dummy_count = dummies;
  config.dummy_change_percent = percent;
  const auto input = PrepareSimulationInput(config, 30U);
  const auto ids = DummyIds(input);
  ASSERT_EQ(input.dummy_steps.size(), 30U);
  for (const auto& step : input.dummy_steps) {
    ASSERT_EQ(step.size(), expected_changes);
    EXPECT_TRUE(std::is_sorted(step.begin(), step.end()));
    EXPECT_EQ(std::adjacent_find(step.begin(), step.end()), step.end());
    for (const auto id : step) {
      ASSERT_LT(id, input.initial_objects.size());
      EXPECT_EQ(input.initial_objects[id].kind, ObjectKind::Dummy);
      EXPECT_EQ(input.initial_objects[id].logical_id, id);
    }
    if (percent == 100U) {
      EXPECT_EQ(step, ids);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    CountsAndPercentages, SimulationInputPlanTest,
    testing::Values(std::tuple{0U, 0U, 0U, 0U}, std::tuple{0U, 0U, 100U, 0U},
                    std::tuple{5U, 0U, 37U, 0U}, std::tuple{5U, 0U, 100U, 0U},
                    std::tuple{0U, 13U, 0U, 0U}, std::tuple{0U, 13U, 37U, 4U},
                    std::tuple{0U, 13U, 100U, 13U}, std::tuple{3U, 13U, 0U, 0U},
                    std::tuple{3U, 13U, 37U, 4U},
                    std::tuple{3U, 13U, 100U, 13U}, std::tuple{3U, 1U, 99U, 0U},
                    std::tuple{3U, 1U, 100U, 1U}, std::tuple{3U, 99U, 1U, 0U},
                    std::tuple{3U, 100U, 1U, 1U},
                    std::tuple{3U, 203U, 37U, 75U}));

TEST(SimulationInputTest,
     InitialStateDoesNotDependOnPlanLengthOrChangePercent) {
  BoidSimulationConfig config;
  config.dummy_count = 15U;
  const auto initial_only = PrepareSimulationInput(config, 0U);
  for (const auto percent : {0U, 37U, 100U}) {
    config.dummy_change_percent = percent;
    ExpectSameInitialObjects(initial_only, PrepareSimulationInput(config, 12U));
  }
}

TEST(SimulationInputTest,
     PlanRandomStreamIsIndependentOfInitialStateGeneration) {
  BoidSimulationConfig config;
  config.boid_count = 0U;
  config.dummy_count = 19U;
  config.dummy_change_percent = 40U;
  const auto dummy_only = PrepareSimulationInput(config, 20U);
  config.boid_count = 7U;
  config.spawn_half_extent = 0.0F;
  config.initial_speed = 0.0F;
  const auto mixed = PrepareSimulationInput(config, 20U);
  const auto mixed_dummy_ids = DummyIds(mixed);
  for (std::size_t step = 0; step < dummy_only.dummy_steps.size(); ++step) {
    std::vector<std::size_t> mapped_ids;
    // Dummy-only logical IDs are ordinals; mixed IDs include interleaved Boids.
    for (const auto ordinal : dummy_only.dummy_steps[step]) {
      mapped_ids.push_back(mixed_dummy_ids.at(ordinal));
    }
    EXPECT_EQ(mapped_ids, mixed.dummy_steps[step]);
  }
}

TEST(SimulationInputTest, LongerHorizonPreservesWarmupAndMeasurementPrefix) {
  BoidSimulationConfig config;
  config.dummy_count = 20U;
  config.dummy_change_percent = 50U;
  constexpr std::size_t kWarmup = 7U;
  constexpr std::size_t kMeasured = 11U;
  const auto warmup = PrepareSimulationInput(config, kWarmup);
  const auto run = PrepareSimulationInput(config, kWarmup + kMeasured);
  const auto longer_run =
      PrepareSimulationInput(config, kWarmup + kMeasured + 9U);
  ExpectSameInitialObjects(warmup, run);
  ExpectSameInitialObjects(run, longer_run);
  for (std::size_t step = 0; step < kWarmup; ++step) {
    EXPECT_EQ(warmup.dummy_steps[step], run.dummy_steps[step]);
  }
  for (std::size_t step = 0; step < kWarmup + kMeasured; ++step) {
    EXPECT_EQ(run.dummy_steps[step], longer_run.dummy_steps[step]);
  }
  // The measured portion continues the stream, rather than replaying its start.
  EXPECT_FALSE(std::equal(run.dummy_steps.begin(),
                          run.dummy_steps.begin() + kMeasured,
                          run.dummy_steps.begin() + kWarmup));
}

TEST(SimulationInputTest, RejectsInvalidPercentEvenWithoutDummiesOrSteps) {
  BoidSimulationConfig config;
  config.boid_count = 0U;
  for (const auto percent : {101U, std::numeric_limits<std::uint32_t>::max()}) {
    config.dummy_change_percent = percent;
    EXPECT_THROW(static_cast<void>(PrepareSimulationInput(config, 0U)),
                 std::invalid_argument);
  }
}

TEST(SimulationInputTest, RejectsInvalidInitialStateConfiguration) {
  constexpr float kInfinity = std::numeric_limits<float>::infinity();
  constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
  for (const float value : {-1.0F, 0.0F, kInfinity, -kInfinity, kNan}) {
    BoidSimulationConfig config;
    config.boundary_half_extent = value;
    EXPECT_THROW(static_cast<void>(PrepareSimulationInput(config, 0U)),
                 std::invalid_argument);
  }
  for (const float value : {-1.0F, 26.0F, kInfinity, -kInfinity, kNan}) {
    BoidSimulationConfig config;
    config.spawn_half_extent = value;
    EXPECT_THROW(static_cast<void>(PrepareSimulationInput(config, 0U)),
                 std::invalid_argument);
  }
  for (const float value : {-1.0F, kInfinity, -kInfinity, kNan}) {
    BoidSimulationConfig config;
    config.initial_speed = value;
    EXPECT_THROW(static_cast<void>(PrepareSimulationInput(config, 0U)),
                 std::invalid_argument);
  }
  BoidSimulationConfig config;
  config.spawn_half_extent = std::numeric_limits<float>::max();
  config.boundary_half_extent = config.spawn_half_extent;
  EXPECT_THROW(static_cast<void>(PrepareSimulationInput(config, 0U)),
               std::invalid_argument);
}

TEST(SimulationInputTest, RejectsNonfiniteOrNegativeBehaviorParameters) {
  constexpr std::array members{&boids::BoidParameters::neighbor_radius,
                               &boids::BoidParameters::separation_radius,
                               &boids::BoidParameters::separation_weight,
                               &boids::BoidParameters::alignment_weight,
                               &boids::BoidParameters::cohesion_weight,
                               &boids::BoidParameters::preferred_speed,
                               &boids::BoidParameters::max_speed,
                               &boids::BoidParameters::max_alignment_force};
  for (const auto member : members) {
    for (const float value : {-1.0F, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
      BoidSimulationConfig config;
      config.parameters.*member = value;
      EXPECT_THROW(static_cast<void>(PrepareSimulationInput(config, 0U)),
                   std::invalid_argument);
    }
  }
}

TEST(SimulationInputTest, RejectsObjectCountOverflowBeforeAllocation) {
  BoidSimulationConfig config;
  config.boid_count = std::numeric_limits<std::size_t>::max();
  config.dummy_count = 1U;
  EXPECT_THROW(static_cast<void>(PrepareSimulationInput(config, 0U)),
               std::overflow_error);
  config.boid_count = 1U;
  config.dummy_count = std::numeric_limits<std::size_t>::max();
  config.dummy_change_percent = 100U;
  EXPECT_THROW(static_cast<void>(PrepareSimulationInput(config, 0U)),
               std::overflow_error);
}

TEST(SimulationInputTest,
     RejectsUnrepresentableContainerSizesBeforeAllocation) {
  BoidSimulationConfig config;
  config.boid_count = std::vector<boids::InitialObjectState>{}.max_size() + 1U;
  EXPECT_THROW(static_cast<void>(PrepareSimulationInput(config, 0U)),
               std::overflow_error);
  config.boid_count = 0U;
  config.dummy_count = std::numeric_limits<std::size_t>::max();
  config.dummy_change_percent = 100U;
  EXPECT_THROW(static_cast<void>(PrepareSimulationInput(config, 0U)),
               std::overflow_error);
  config.dummy_count = 0U;
  const auto steps = std::vector<boids::DummyStepPlan>{}.max_size() + 1U;
  EXPECT_THROW(static_cast<void>(PrepareSimulationInput(config, steps)),
               std::overflow_error);
}

TEST(SimulationInputTest, RejectsPlanEntryAndByteOverflowBeforeAllocation) {
  BoidSimulationConfig config;
  config.boid_count = 0U;
  config.dummy_count = 100U;
  config.dummy_change_percent = 100U;
  constexpr auto kMax = std::numeric_limits<std::size_t>::max();
  const auto entry_overflow_steps = kMax / config.dummy_count + 1U;
  EXPECT_THROW(
      static_cast<void>(PrepareSimulationInput(config, entry_overflow_steps)),
      std::overflow_error);
  const auto byte_overflow_steps =
      kMax / sizeof(std::size_t) / config.dummy_count + 1U;
  EXPECT_THROW(
      static_cast<void>(PrepareSimulationInput(config, byte_overflow_steps)),
      std::overflow_error);
  // Each allocation size fits, but the combined plan storage does not.
  const auto total_overflow_steps =
      kMax / (sizeof(boids::DummyStepPlan) +
              config.dummy_count * sizeof(std::size_t)) +
      1U;
  EXPECT_THROW(
      static_cast<void>(PrepareSimulationInput(config, total_overflow_steps)),
      std::overflow_error);
}

}  // namespace
