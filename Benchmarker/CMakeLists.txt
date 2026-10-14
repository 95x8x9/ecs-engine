add_library(BenchmarkerExperiment STATIC benchmarker.cpp)
add_library(Benchmarker::Experiment ALIAS BenchmarkerExperiment)
target_include_directories(BenchmarkerExperiment PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(BenchmarkerExperiment PUBLIC cxx_std_20)
target_link_libraries(BenchmarkerExperiment PUBLIC Engine::Measurement Boids::Common)
target_compile_definitions(BenchmarkerExperiment PRIVATE
    BENCHMARKER_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    BENCHMARKER_BUILD_CONFIG="$<CONFIG>"
    BENCHMARKER_BUILD_FLAGS="${CMAKE_CXX_FLAGS} $<$<CONFIG:Debug>:${CMAKE_CXX_FLAGS_DEBUG}>$<$<CONFIG:Release>:${CMAKE_CXX_FLAGS_RELEASE}>$<$<CONFIG:RelWithDebInfo>:${CMAKE_CXX_FLAGS_RELWITHDEBINFO}>$<$<CONFIG:MinSizeRel>:${CMAKE_CXX_FLAGS_MINSIZEREL}>"
)
if(MSVC)
    target_compile_options(BenchmarkerExperiment PRIVATE /W4)
else()
    target_compile_options(BenchmarkerExperiment PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_link_libraries(BenchmarkerExperiment PRIVATE advapi32)
endif()
