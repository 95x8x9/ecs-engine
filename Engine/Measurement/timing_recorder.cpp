#include "Engine/Measurement/timing_recorder.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace engine::measurement {

TimingRecorder::TimingRecorder(
    std::initializer_list<std::string_view> sections) {
  for (auto name : sections) {
    static_cast<void>(register_section(name));
  }
}

TimingRecorder::SectionId TimingRecorder::register_section(
    std::string_view name) {
  if (prepared_ || any_running()) {
    fail("Register sections before recording");
  }
  if (name.empty()) {
    throw std::invalid_argument("Section name must not be empty");
  }
  for (SectionId id = 0; id < sections_.size(); ++id) {
    if (sections_[id].name == name) {
      return id;
    }
  }
  sections_.push_back(Section{.name = std::string(name)});
  return sections_.size() - 1U;
}

TimingRecorder::SectionId TimingRecorder::find(std::string_view name) {
  for (SectionId id = 0; id < sections_.size(); ++id) {
    if (sections_[id].name == name) {
      return id;
    }
  }
  fail("Unknown section; register it before recording");
}

TimingRecorder::Section& TimingRecorder::section(SectionId id) {
  if (id >= sections_.size()) {
    fail("Invalid section ID");
  }
  return sections_[id];
}

void TimingRecorder::start(std::string_view name) {
  if (enabled_) start(find(name));
}

void TimingRecorder::stop(std::string_view name) {
  if (enabled_) stop(find(name));
}

void TimingRecorder::start(SectionId id) {
  if (!enabled_) return;
  auto& entry = section(id);
  if (invalid_ || entry.running || (prepared_ && !in_step_)) {
    fail("Cannot start section in this state");
  }
  entry.running = true;
  entry.begin = Clock::now();
}

void TimingRecorder::stop(SectionId id) {
  if (!enabled_) return;
  const auto end = Clock::now();
  auto& entry = section(id);
  if (invalid_ || !entry.running) {
    fail("Section must be started before stopping");
  }
  const double elapsed =
      std::chrono::duration<double, std::milli>(end - entry.begin).count();
  entry.running = false;
  entry.total_ms += elapsed;
  entry.pending.elapsed_ms += elapsed;
  ++entry.pending.calls;
  entry.pending.recorded = true;
}

void TimingRecorder::record_zero(std::string_view name) {
  if (!enabled_) return;
  auto& entry = section(find(name));
  if (invalid_ || entry.running || (prepared_ && !in_step_)) {
    fail("Cannot record zero in this state");
  }
  entry.pending.recorded = true;
}

bool TimingRecorder::any_running() const {
  return std::any_of(sections_.begin(), sections_.end(),
                     [](const Section& value) { return value.running; });
}

void TimingRecorder::set_enabled(bool enabled) {
  if (invalid_ || in_step_ || any_running() || prepared_) {
    fail("Enable or disable instrumentation before measurement");
  }
  enabled_ = enabled;
}

void TimingRecorder::begin_measurement(std::size_t steps) {
  if (invalid_ || in_step_ || any_running() || prepared_) {
    fail("Measurement can only be prepared once, before timing");
  }
  if (steps == 0U) {
    throw std::invalid_argument("Measured steps must be positive");
  }
  step_ms_.resize(steps);
  for (auto& entry : sections_) {
    entry.samples.resize(steps);
    entry.total_ms = 0.0;
    entry.pending = {};
  }
  prepared_ = true;
  enabled_ = true;
}

void TimingRecorder::begin_step_measurement() {
  if (invalid_ || !prepared_ || in_step_ || any_running() ||
      completed_steps_ >= step_ms_.size()) {
    fail("Cannot begin step in this state");
  }
  for (auto& entry : sections_) entry.pending = {};
  in_step_ = true;
  step_begin_ = Clock::now();
}

void TimingRecorder::end_step_measurement() {
  const auto end = Clock::now();
  if (invalid_ || !in_step_ || any_running()) {
    fail("End all sections before ending the step");
  }
  // Copy samples after the overall interval has ended.
  step_ms_[completed_steps_] =
      std::chrono::duration<double, std::milli>(end - step_begin_).count();
  for (auto& entry : sections_) {
    entry.samples[completed_steps_] = entry.pending;
  }
  ++completed_steps_;
  in_step_ = false;
}

double TimingRecorder::total_ms(std::string_view name) const {
  if (invalid_ || any_running()) {
    throw std::logic_error("Cannot read unfinished or invalid timings");
  }
  for (const auto& entry : sections_) {
    if (entry.name == name) return entry.total_ms;
  }
  throw std::invalid_argument("Unknown section");
}

TimingResult TimingRecorder::result() const {
  if (invalid_ || !prepared_ || in_step_ || any_running() ||
      completed_steps_ != step_ms_.size()) {
    throw std::logic_error(
        "Cannot export an incomplete or invalid measurement");
  }
  TimingResult output;
  output.measured_steps = completed_steps_;
  output.step_ms = step_ms_;
  for (double elapsed : step_ms_) output.total_update_ms += elapsed;
  output.mean_update_ms =
      output.total_update_ms / static_cast<double>(completed_steps_);
  if (output.mean_update_ms > 0.0) {
    output.updates_per_second = 1000.0 / output.mean_update_ms;
  }
  for (const auto& entry : sections_) {
    SectionResult result{.name = entry.name,
                         .samples = entry.samples,
                         .total_ms = entry.total_ms};
    for (const auto& sample : entry.samples) {
      result.calls += sample.calls;
      result.recorded_steps += sample.recorded ? 1U : 0U;
    }
    if (result.recorded_steps == completed_steps_) {
      result.mean_ms_per_step =
          entry.total_ms / static_cast<double>(completed_steps_);
    }
    output.sections.push_back(std::move(result));
  }
  return output;
}

void TimingRecorder::fail(const char* message) {
  invalid_ = true;
  throw std::logic_error(message);
}

}  // namespace engine::measurement
