#pragma once

#include "Boids/Common/boid_simulation_config.h"
#include "Boids/ECS/Systems/boid_system.h"
#include "Engine/ECS/registry.h"
#include "Engine/ECS/system.h"

namespace boids::ecs {

class BoidSimulation {
 public:
  explicit BoidSimulation(boids::BoidSimulationConfig config = {});

  void Initialize();
  void Update(float delta_seconds);
  void Update(float delta_seconds,
              engine::measurement::TimingRecorder& timing_recorder);
  [[nodiscard]] engine::measurement::StorageReport GetStorageReport() const;

  [[nodiscard]] engine::ecs::Registry& GetRegistry() { return registry_; }
  [[nodiscard]] const engine::ecs::Registry& GetRegistry() const {
    return registry_;
  }

 private:
  boids::BoidSimulationConfig config_;
  engine::ecs::Registry registry_;
  BoidSystem boid_system_;
  engine::ecs::SystemManager system_manager_;
};

}  // namespace boids::ecs
