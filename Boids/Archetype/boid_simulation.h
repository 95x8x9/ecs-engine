#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "Boids/Common/boid_simulation_config.h"
#include "Boids/Common/simulation_input.h"
#include "Engine/ECS/Archetype/registry.h"

namespace engine::measurement {
class TimingRecorder;
}

namespace boids::archetype {

class BoidSimulation {
 public:
  explicit BoidSimulation(boids::BoidSimulationConfig config = {});

  // The caller keeps input alive and unchanged through the last Update/report.
  void Initialize(const boids::SimulationInput& input);
  // Pass a unique Dummy list prepared by Common. There is no internal plan
  // cursor; warmup and measurement use consecutive plans from the caller.
  void Update(float dt, std::span<const std::size_t> dummy_ids,
              engine::measurement::TimingRecorder& timing);
  [[nodiscard]] engine::measurement::StorageReport GetStorageReport() const;
  [[nodiscard]] const engine::ecs::archetype::Registry& GetRegistry() const {
    return registry_;
  }
  [[nodiscard]] engine::ecs::Entity EntityFor(std::size_t logical_id) const;

 private:
  void ValidateInput(const boids::SimulationInput& input) const;
  void ChangeDummies(std::span<const std::size_t> dummy_ids);

  boids::BoidSimulationConfig config_;
  engine::ecs::archetype::Registry registry_;
  const boids::SimulationInput* input_{nullptr};
  std::vector<engine::ecs::Entity> entities_;
  bool initialized_{false};
};

}  // namespace boids::archetype
