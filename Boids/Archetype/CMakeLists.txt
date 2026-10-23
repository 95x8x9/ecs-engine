add_library(BoidsArchetype STATIC boid_simulation.cpp)
add_library(Boids::Archetype ALIAS BoidsArchetype)

target_include_directories(BoidsArchetype PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(BoidsArchetype PUBLIC cxx_std_20)
target_link_libraries(BoidsArchetype
    PUBLIC Boids::Common Engine::ArchetypeECS Engine::RenderData Engine::Measurement
)

if(MSVC)
    target_compile_options(BoidsArchetype PRIVATE /W4)
else()
    target_compile_options(BoidsArchetype PRIVATE -Wall -Wextra -Wpedantic)
endif()
