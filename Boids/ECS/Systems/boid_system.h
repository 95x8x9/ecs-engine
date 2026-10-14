#pragma once

#include <glm/vec3.hpp>
#include <optional>

#include "Boids/Common/boid_parameters.h"
#include "Boids/ECS/Components/velocity.h"
#include "Engine/ECS/entity.h"
#include "Engine/ECS/system.h"
#include "Engine/Measurement/timing_recorder.h"
#include "Engine/Renderer/transform.h"

namespace boids::ecs {

class BoidSystem final : public engine::ecs::System {
 public:
  using engine::ecs::System::Update;
  void Update(engine::ecs::Registry& registry, float delta_seconds,
              engine::measurement::TimingRecorder& timing) override;
  void AppendStorageReport(
      engine::measurement::StorageReport& report) const override;

 private:
  using NeighborView =
      engine::ecs::CachedView<engine::renderer::Transform, Velocity>;

  [[nodiscard]] glm::vec3 CalculateAcceleration(
      NeighborView& neighbor_view, engine::ecs::Entity self,
      const engine::renderer::Transform& self_transform,
      const Velocity& self_velocity,
      const BoidParameters& self_parameters) const;

  std::optional<NeighborView> neighbor_view_;
};

}  // namespace boids::ecs
