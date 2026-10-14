#pragma once

#include "Engine/ECS/system.h"

namespace boids::ecs {

class BoidOrientationSystem final : public engine::ecs::System {
 public:
  using engine::ecs::System::Update;
  void Update(engine::ecs::Registry& registry, float delta_seconds,
              engine::measurement::TimingRecorder& timing) override;
};

}  // namespace boids::ecs
