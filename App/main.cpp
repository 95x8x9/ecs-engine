#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "App/UI/performance_panel.h"
#include "App/command_line_options.h"
#include "Boids/Common/boid_simulation_config.h"
#include "Boids/ECS/boid_simulation.h"
#include "Boids/OOP/boid_simulation.h"
#include "Engine/Core/application.h"
#include "Engine/ECSRenderer/ecs_render_system.h"
#include "Engine/Renderer/agent_render_manager.h"

int main(int argc, char* argv[]) try {
  std::vector<std::string_view> arguments;
  for (int index = 1; index < argc; ++index) {
    arguments.emplace_back(argv[index]);
  }
  const auto options = app::ParseCommandLineOptions(arguments);
  if (options.help) {
    app::PrintUsage(std::cout);
    return 0;
  }

  if (options.benchmark_enabled) {
    benchmarker::ExperimentResult result;

    switch (options.implementation) {
      case app::SimulationImplementation::Ecs:
        result = benchmarker::RunBenchmark<boids::ecs::BoidSimulation>(
            options.simulation_config, options.benchmark_config, "ecs");
        break;
      case app::SimulationImplementation::Oop:
        result = benchmarker::RunBenchmark<boids::oop::BoidSimulation>(
            options.simulation_config, options.benchmark_config, "oop");
        break;
    }

    std::filesystem::path output_path(options.output_path);
    if (output_path.empty()) {
      for (const auto& [name, value] : result.environment) {
        if (name == "run_id") {
          output_path = std::filesystem::path("out/benchmarks") /
                        (result.implementation + "-" + value + ".csv");
          break;
        }
      }
    }
    if (!output_path.parent_path().empty()) {
      std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream output(output_path);
    if (!output) throw std::runtime_error("Cannot open benchmark output file");
    benchmarker::WriteCsv(output, result);
    output.close();
    if (!output) throw std::runtime_error("Cannot write benchmark output file");
    benchmarker::PrintSummary(std::cout, result);
    std::cout << "Raw results: " << output_path.string() << '\n';
    return 0;
  }

  engine::core::Application application;
  const boids::BoidSimulationConfig simulation_config;
  boids::ecs::BoidSimulation ecs_simulation(simulation_config);
  engine::renderer::AgentRenderManager render_manager(
      simulation_config.boundary_half_extent);
  engine::ecs_renderer::EcsRenderSystem ecs_render_system;
  app::ui::PerformancePanel performance_panel;

  if (!application.Initialize()) {
    return 1;
  }

  ecs_simulation.Initialize();
  if (!render_manager.Initialize()) {
    return 1;
  }

  application.Run(
      [&ecs_simulation, &render_manager](
          float delta_seconds, const engine::input::InputState& input_state) {
        ecs_simulation.Update(delta_seconds);
        render_manager.UpdateCamera(input_state, delta_seconds);
      },
      [&ecs_simulation, &render_manager, &ecs_render_system](
          int framebuffer_width, int framebuffer_height) {
        ecs_render_system.Render(ecs_simulation.GetRegistry(), render_manager,
                                 framebuffer_width, framebuffer_height);
      },
      [&application, &performance_panel,
       boid_count = simulation_config.boid_count]() {
        bool vsync_enabled = application.IsVSyncEnabled();
        if (performance_panel.Draw(boid_count, vsync_enabled)) {
          application.SetVSyncEnabled(vsync_enabled);
        }
      });
  return 0;
} catch (const std::exception& error) {
  std::cerr << "Error: " << error.what() << '\n';
  return 1;
}
