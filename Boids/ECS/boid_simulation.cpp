#include "Boids/ECS/boid_simulation.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <random>
#include <stdexcept>
#include <utility>

#include "Boids/ECS/Components/acceleration.h"
#include "Boids/ECS/Components/velocity.h"
#include "Boids/ECS/Systems/boid_orientation_system.h"
#include "Boids/ECS/Systems/boid_system.h"
#include "Boids/ECS/Systems/boundary_system.h"
#include "Boids/ECS/Systems/movement_system.h"
#include "Engine/Renderer/mesh_renderer.h"
#include "Engine/Renderer/transform.h"

namespace boids::ecs {

// Validate config and add systems to system manager
BoidSimulation::BoidSimulation(boids::BoidSimulationConfig config)
    : config_(std::move(config)) {
  if (config_.boundary_half_extent <= 0.0F) {
    throw std::invalid_argument("Boundary half extent must be positive");
  }
  if (config_.spawn_half_extent < 0.0F ||
      config_.spawn_half_extent > config_.boundary_half_extent) {
    throw std::invalid_argument(
        "Spawn half extent must be inside the simulation boundary");
  }
  if (config_.initial_speed < 0.0F) {
    throw std::invalid_argument("Initial speed cannot be negative");
  }

  system_manager_.Add<MovementSystem>();
  system_manager_.Add<BoundarySystem>(config_.boundary_half_extent);
  system_manager_.Add<BoidOrientationSystem>();
}

// Initalize entities to simulation
void BoidSimulation::Initialize() {
  registry_.Clear();

  std::mt19937 random_engine{config_.random_seed};
  std::uniform_real_distribution<float> position_distribution(
      -config_.spawn_half_extent, config_.spawn_half_extent);
  std::uniform_real_distribution<float> direction_distribution(-1.0F, 1.0F);

  for (std::size_t index = 0; index < config_.boid_count; ++index) {
    const engine::ecs::Entity entity = registry_.Create();
    const glm::vec3 position{position_distribution(random_engine),
                             position_distribution(random_engine),
                             position_distribution(random_engine)};

    glm::vec3 direction{direction_distribution(random_engine),
                        direction_distribution(random_engine),
                        direction_distribution(random_engine)};
    if (glm::dot(direction, direction) <= 0.00000001F) {
      direction = glm::vec3{1.0F, 0.0F, 0.0F};
    } else {
      direction = glm::normalize(direction);
    }
    const glm::vec3 velocity = direction * config_.initial_speed;

    registry_.Emplace<engine::renderer::Transform>(
        entity,
        engine::renderer::Transform{
            .position = position,
            .rotation = engine::renderer::RotationFromDirection(velocity),
            .scale = 0.7F});
    registry_.Emplace<engine::renderer::MeshRenderer>(entity);
    registry_.Emplace<Velocity>(entity, Velocity{.value = velocity});
    registry_.Emplace<Acceleration>(entity, Acceleration{});
    registry_.Emplace<BoidParameters>(entity, config_.parameters);
  }
}

void BoidSimulation::Update(float delta_seconds) {
  engine::measurement::TimingRecorder disabled;
  disabled.set_enabled(false);
  Update(delta_seconds, disabled);
}

void BoidSimulation::Update(float delta_seconds,
                            engine::measurement::TimingRecorder& timing) {
  if (delta_seconds < 0.0F) {
    throw std::invalid_argument("Delta time cannot be negative");
  }
  // Dummy churn is not implemented yet; current entity changes require no work.
  timing.record_zero("entity_changes");
  boid_system_.Update(registry_, delta_seconds, timing);

  timing.start("state_update");
  system_manager_.Update(registry_, delta_seconds);
  timing.stop("state_update");
}

engine::measurement::StorageReport BoidSimulation::GetStorageReport() const {
  engine::measurement::StorageReport report;
  report.add_objects<BoidSimulation>("simulation.object", 1U);
  registry_.AppendStorageReport(report);
  boid_system_.AppendStorageReport(report);
  system_manager_.AppendStorageReport(report);
  report.add_objects<MovementSystem>("systems.movement", 1U);
  report.add_objects<BoundarySystem>("systems.boundary", 1U);
  report.add_objects<BoidOrientationSystem>("systems.orientation", 1U);
  return report;
}

}  // namespace boids::ecs
