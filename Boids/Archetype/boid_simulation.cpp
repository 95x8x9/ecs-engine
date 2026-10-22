#include "Boids/Archetype/boid_simulation.h"

#include <cmath>
#include <glm/geometric.hpp>
#include <initializer_list>
#include <limits>
#include <stdexcept>

#include "Boids/ECS/Components/acceleration.h"
#include "Boids/ECS/Components/velocity.h"
#include "Engine/Measurement/timing_recorder.h"
#include "Engine/Renderer/transform.h"

namespace boids::archetype {
namespace {

using boids::ecs::Acceleration;
using boids::ecs::Velocity;
using engine::renderer::Transform;

constexpr float kMinVectorLengthSquared = 0.00000001F;

bool IsFinite(const glm::vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

Transform InitialTransform(const boids::InitialObjectState& initial) {
  return {.position = initial.position,
          .rotation = engine::renderer::RotationFromDirection(initial.velocity),
          .scale = 0.7F};
}

glm::vec3 CalculateAlignment(const glm::vec3& velocity_sum,
                             std::size_t neighbor_count,
                             const Velocity& velocity,
                             const boids::BoidParameters& parameters) {
  const glm::vec3 average = velocity_sum / static_cast<float>(neighbor_count);
  if (glm::dot(average, average) <= kMinVectorLengthSquared ||
      parameters.max_alignment_force <= 0.0F) {
    return glm::vec3{0.0F};
  }
  const glm::vec3 desired =
      glm::normalize(average) * parameters.preferred_speed;
  const glm::vec3 steering = desired - velocity.value;
  if (glm::dot(steering, steering) <=
      parameters.max_alignment_force * parameters.max_alignment_force) {
    return steering;
  }
  return glm::normalize(steering) * parameters.max_alignment_force;
}

void CalculateAccelerations(std::span<const Transform> transforms,
                            std::span<const Velocity> velocities,
                            std::span<const boids::BoidParameters> parameters,
                            std::span<Acceleration> accelerations) {
  for (std::size_t self = 0; self < transforms.size(); ++self) {
    glm::vec3 separation{0.0F};
    glm::vec3 velocity_sum{0.0F};
    glm::vec3 position_sum{0.0F};
    std::size_t neighbor_count = 0U;
    const auto& settings = parameters[self];
    for (std::size_t other = 0; other < transforms.size(); ++other) {
      if (other == self) continue;
      const glm::vec3 offset =
          transforms[other].position - transforms[self].position;
      const float distance_squared = glm::dot(offset, offset);
      if (distance_squared >=
          settings.neighbor_radius * settings.neighbor_radius) {
        continue;
      }
      ++neighbor_count;
      velocity_sum += velocities[other].value;
      position_sum += transforms[other].position;
      if (distance_squared > kMinVectorLengthSquared &&
          distance_squared <
              settings.separation_radius * settings.separation_radius) {
        separation -= offset / distance_squared;
      }
    }
    if (neighbor_count == 0U) {
      accelerations[self].value = glm::vec3{0.0F};
      continue;
    }
    const auto alignment = CalculateAlignment(velocity_sum, neighbor_count,
                                              velocities[self], settings);
    const glm::vec3 cohesion =
        position_sum / static_cast<float>(neighbor_count) -
        transforms[self].position;
    accelerations[self].value = separation * settings.separation_weight +
                                alignment * settings.alignment_weight +
                                cohesion * settings.cohesion_weight;
  }
}

void UpdateStates(std::span<Transform> transforms,
                  std::span<Velocity> velocities,
                  std::span<const Acceleration> accelerations,
                  std::span<const boids::BoidParameters> parameters, float dt,
                  float boundary_half_extent) {
  for (std::size_t row = 0; row < transforms.size(); ++row) {
    auto& velocity = velocities[row].value;
    velocity += accelerations[row].value * dt;
    const float speed = glm::length(velocity);
    if (parameters[row].max_speed <= 0.0F) {
      velocity = glm::vec3{0.0F};
    } else if (speed > parameters[row].max_speed) {
      velocity = glm::normalize(velocity) * parameters[row].max_speed;
    }
    transforms[row].position += velocity * dt;
  }
  for (std::size_t row = 0; row < transforms.size(); ++row) {
    for (int axis = 0; axis < 3; ++axis) {
      auto& position = transforms[row].position[axis];
      auto& velocity = velocities[row].value[axis];
      if (position < -boundary_half_extent) {
        position = -boundary_half_extent;
        velocity = std::abs(velocity);
      } else if (position > boundary_half_extent) {
        position = boundary_half_extent;
        velocity = -std::abs(velocity);
      }
    }
  }
  for (std::size_t row = 0; row < transforms.size(); ++row) {
    transforms[row].rotation =
        engine::renderer::RotationFromDirection(velocities[row].value);
  }
}

}  // namespace

BoidSimulation::BoidSimulation(boids::BoidSimulationConfig config)
    : config_(config) {
  if (!std::isfinite(config_.boundary_half_extent) ||
      config_.boundary_half_extent <= 0.0F) {
    throw std::invalid_argument(
        "Boundary half extent must be finite and positive");
  }
  if (!std::isfinite(config_.spawn_half_extent) ||
      config_.spawn_half_extent < 0.0F ||
      config_.spawn_half_extent > config_.boundary_half_extent ||
      config_.spawn_half_extent > std::numeric_limits<float>::max() / 2.0F) {
    throw std::invalid_argument(
        "Spawn range must be finite, representable and inside the boundary");
  }
  const auto& parameters = config_.parameters;
  for (const float value :
       {config_.initial_speed, parameters.neighbor_radius,
        parameters.separation_radius, parameters.separation_weight,
        parameters.alignment_weight, parameters.cohesion_weight,
        parameters.preferred_speed, parameters.max_speed,
        parameters.max_alignment_force}) {
    if (!std::isfinite(value) || value < 0.0F) {
      throw std::invalid_argument(
          "Initial speed and Boid parameters must be finite and nonnegative");
    }
  }
  if (config_.dummy_change_percent > 100U) {
    throw std::invalid_argument("Dummy change percent must be in [0, 100]");
  }
  if (config_.boid_count >
          std::numeric_limits<std::size_t>::max() - config_.dummy_count ||
      config_.boid_count + config_.dummy_count >
          engine::ecs::Entity::kInvalidId) {
    throw std::overflow_error("Object count exceeds Archetype Entity IDs");
  }
}

void BoidSimulation::ValidateInput(const boids::SimulationInput& input) const {
  if (input.initial_objects.size() !=
      config_.boid_count + config_.dummy_count) {
    throw std::invalid_argument("Input object count does not match config");
  }
  std::size_t boid_count = 0U;
  std::size_t dummy_count = 0U;
  for (std::size_t index = 0; index < input.initial_objects.size(); ++index) {
    const auto& initial = input.initial_objects[index];
    if (initial.logical_id != index) {
      throw std::invalid_argument(
          "Input logical ID must equal its initial index");
    }
    if (!IsFinite(initial.position) || !IsFinite(initial.velocity)) {
      throw std::invalid_argument("Input position and velocity must be finite");
    }
    switch (initial.kind) {
      case boids::ObjectKind::Boid:
        ++boid_count;
        break;
      case boids::ObjectKind::Dummy:
        ++dummy_count;
        break;
      default:
        throw std::invalid_argument("Unknown input object kind");
    }
  }
  if (boid_count != config_.boid_count || dummy_count != config_.dummy_count) {
    throw std::invalid_argument("Input object kinds do not match config");
  }
}

void BoidSimulation::Initialize(const boids::SimulationInput& input) {
  ValidateInput(input);
  initialized_ = false;
  input_ = nullptr;
  entities_.clear();
  registry_.Clear();
  // Prepare both signatures even when one kind has no rows. Update only looks
  // up existing storage and never creates tables during query preparation.
  static_cast<void>(
      registry_.GetOrCreateTable<Transform, Velocity, Acceleration,
                                 boids::BoidParameters>());
  static_cast<void>(registry_.GetOrCreateTable<Transform, Velocity>());
  entities_.reserve(input.initial_objects.size());
  for (const auto& initial : input.initial_objects) {
    const auto transform = InitialTransform(initial);
    const auto velocity = Velocity{initial.velocity};
    entities_.push_back(initial.kind == boids::ObjectKind::Boid
                            ? registry_.Create(transform, velocity,
                                               Acceleration{},
                                               config_.parameters)
                            : registry_.Create(transform, velocity));
  }
  input_ = &input;
  initialized_ = true;
}

engine::ecs::Entity BoidSimulation::EntityFor(std::size_t logical_id) const {
  if (logical_id >= entities_.size() || !entities_[logical_id].IsValid()) {
    throw std::out_of_range("No Entity for logical ID");
  }
  return entities_[logical_id];
}

void BoidSimulation::ChangeDummies(std::span<const std::size_t> dummy_ids) {
  // A failed change requires reinitialization; never run another step with an
  // incomplete mapping. No spans or table references cross structural changes.
  initialized_ = false;
  for (const auto id : dummy_ids) {
    const auto entity = EntityFor(id);
    if (input_->initial_objects[id].kind != boids::ObjectKind::Dummy) {
      throw std::invalid_argument("Only Dummy entities may be replaced");
    }
    registry_.Destroy(entity);
    entities_[id] = engine::ecs::Entity{};
  }
  for (const auto id : dummy_ids) {
    const auto& initial = input_->initial_objects[id];
    entities_[id] =
        registry_.Create(InitialTransform(initial), Velocity{initial.velocity});
  }
  initialized_ = true;
}

void BoidSimulation::Update(float dt, std::span<const std::size_t> dummy_ids,
                            engine::measurement::TimingRecorder& timing) {
  if (!initialized_) {
    throw std::logic_error("Simulation must be initialized before Update");
  }
  if (!std::isfinite(dt) || dt < 0.0F) {
    throw std::invalid_argument("Delta time must be finite and nonnegative");
  }
  if (dummy_ids.empty()) {
    timing.record_zero("entity_changes");
  } else {
    timing.start("entity_changes");
    ChangeDummies(dummy_ids);
    timing.stop("entity_changes");
  }

  timing.start("query_preparation");
  auto& table = registry_.GetOrCreateTable<Transform, Velocity, Acceleration,
                                           boids::BoidParameters>();
  const auto transforms = table.Column<Transform>();
  const auto velocities = table.Column<Velocity>();
  const auto accelerations = table.Column<Acceleration>();
  const auto parameters = table.Column<boids::BoidParameters>();
  timing.stop("query_preparation");

  if (transforms.empty()) {
    timing.record_zero("neighbor_calculation");
    timing.record_zero("state_update");
    return;
  }

  timing.start("neighbor_calculation");
  CalculateAccelerations(transforms, velocities, parameters, accelerations);
  timing.stop("neighbor_calculation");

  timing.start("state_update");
  UpdateStates(transforms, velocities, accelerations, parameters, dt,
               config_.boundary_half_extent);
  timing.stop("state_update");
}

engine::measurement::StorageReport BoidSimulation::GetStorageReport() const {
  engine::measurement::StorageReport report;
  report.add_objects<BoidSimulation>("simulation.object", 1U);
  report.add_vector("entities.logical_ids", entities_);
  registry_.AppendStorageReport(report, "registry");
  return report;
}

}  // namespace boids::archetype
