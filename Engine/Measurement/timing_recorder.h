#pragma once

#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::measurement {

struct SectionSample {
  double elapsed_ms{0.0};
  std::size_t calls{0U};
  bool recorded{false};  // An explicit zero differs from a missing sample.
};

struct SectionResult {
  std::string name;
  std::vector<SectionSample> samples;
  double total_ms{0.0};
  // Missing if any step was unrecorded; total_ms is the known subtotal.
  std::optional<double> mean_ms_per_step;
  std::size_t calls{0U};
  std::size_t recorded_steps{0U};
};

struct TimingResult {
  std::size_t measured_steps{0U};
  double total_update_ms{0.0};
  double mean_update_ms{0.0};
  double updates_per_second{0.0};
  std::vector<double> step_ms;
  std::vector<SectionResult> sections;
};

// Single-threaded recorder. Never invokes simulation code or callbacks.
// Register sections before timing. Enabled start/stop performs no allocation;
// different sections may overlap or nest.
class TimingRecorder {
 public:
  using SectionId = std::size_t;
  explicit TimingRecorder(
      std::initializer_list<std::string_view> sections = {});
  [[nodiscard]] SectionId register_section(std::string_view name);
  void start(std::string_view name);
  void stop(std::string_view name);
  void start(SectionId id);
  void stop(SectionId id);
  // Explicitly marks a section that needs no work in this step.
  void record_zero(std::string_view name);
  // Disabled instrumentation is a no-op, including clock reads and name lookup.
  void set_enabled(bool enabled);
  // Resets timings, preallocates samples and enables recording.
  void begin_measurement(std::size_t steps);
  void begin_step_measurement();
  void end_step_measurement();
  // Standalone intervals (e.g. initialization) need no begin_measurement.
  [[nodiscard]] double total_ms(std::string_view name) const;
  [[nodiscard]] TimingResult result() const;

 private:
  using Clock = std::chrono::steady_clock;
  struct Section {
    std::string name;
    Clock::time_point begin;
    bool running{false};
    SectionSample pending;
    double total_ms{0.0};
    std::vector<SectionSample> samples;
  };
  [[nodiscard]] SectionId find(std::string_view name);
  [[nodiscard]] Section& section(SectionId id);
  [[noreturn]] void fail(const char* message);
  [[nodiscard]] bool any_running() const;
  std::vector<Section> sections_;
  std::vector<double> step_ms_;
  Clock::time_point step_begin_;
  std::size_t completed_steps_{0U};
  bool enabled_{true};
  bool prepared_{false};
  bool in_step_{false};
  bool invalid_{false};
};

}  // namespace engine::measurement
