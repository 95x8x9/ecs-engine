#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include "Engine/Measurement/memory_measurement.h"
#include "Engine/Measurement/timing_recorder.h"

namespace {
using engine::measurement::MeasurementStatus;
using engine::measurement::StorageReport;
using engine::measurement::TimingRecorder;

TEST(TimingRecorderTest, SupportsStandaloneIntervalsAndPreRegisteredIds) {
  TimingRecorder recorder;
  const auto id = recorder.register_section("initialization");
  EXPECT_EQ(id, recorder.register_section("initialization"));
  recorder.start(id);
  recorder.stop("initialization");
  EXPECT_GE(recorder.total_ms("initialization"), 0.0);
}

TEST(TimingRecorderTest, RecordsOverallAndNestedSectionsWithRepeatedCalls) {
  TimingRecorder recorder({"outer", "inner"});
  recorder.begin_measurement(3U);
  for (int step = 0; step < 3; ++step) {
    recorder.begin_step_measurement();
    recorder.start("outer");
    for (int call = 0; call < 2; ++call) {
      recorder.start("inner");
      recorder.stop("inner");
    }
    recorder.stop("outer");
    recorder.end_step_measurement();
  }
  const auto result = recorder.result();
  ASSERT_EQ(result.sections.size(), 2U);
  ASSERT_EQ(result.step_ms.size(), 3U);
  EXPECT_EQ(result.measured_steps, 3U);
  EXPECT_DOUBLE_EQ(result.mean_update_ms, result.total_update_ms / 3.0);
  if (result.mean_update_ms > 0.0) {
    EXPECT_DOUBLE_EQ(result.updates_per_second, 1000.0 / result.mean_update_ms);
  }
  const auto& outer = result.sections[0];
  const auto& inner = result.sections[1];
  EXPECT_EQ(outer.recorded_steps, 3U);
  EXPECT_EQ(outer.calls, 3U);
  EXPECT_EQ(inner.calls, 6U);
  ASSERT_TRUE(inner.mean_ms_per_step.has_value());
  EXPECT_DOUBLE_EQ(*inner.mean_ms_per_step, inner.total_ms / 3.0);
  for (std::size_t step = 0; step < 3U; ++step) {
    EXPECT_EQ(inner.samples[step].calls, 2U);
    EXPECT_LE(inner.samples[step].elapsed_ms, outer.samples[step].elapsed_ms);
    EXPECT_LE(outer.samples[step].elapsed_ms, result.step_ms[step]);
  }
}

TEST(TimingRecorderTest, DisabledWarmupIsExcludedAndZeroDiffersFromMissing) {
  TimingRecorder recorder({"work", "no_work", "missing"});
  recorder.set_enabled(false);
  recorder.start("work");
  recorder.stop("work");
  EXPECT_NO_THROW(recorder.start("unknown_during_warmup"));
  recorder.begin_measurement(2U);
  for (int step = 0; step < 2; ++step) {
    recorder.begin_step_measurement();
    recorder.start("work");
    recorder.stop("work");
    recorder.record_zero("no_work");
    recorder.end_step_measurement();
  }
  const auto result = recorder.result();
  EXPECT_EQ(result.sections[0].calls, 2U);
  EXPECT_EQ(result.sections[1].recorded_steps, 2U);
  EXPECT_DOUBLE_EQ(result.sections[1].total_ms, 0.0);
  EXPECT_EQ(result.sections[1].calls, 0U);
  EXPECT_TRUE(result.sections[1].samples[0].recorded);
  EXPECT_EQ(result.sections[2].recorded_steps, 0U);
  EXPECT_FALSE(result.sections[2].samples[0].recorded);
  EXPECT_FALSE(result.sections[2].mean_ms_per_step.has_value());
}

TEST(TimingRecorderTest, RejectsInvalidIntervalsAndPoisonsInvalidRuns) {
  {
    TimingRecorder recorder({"work"});
    recorder.begin_measurement(1U);
    recorder.begin_step_measurement();
    EXPECT_THROW(recorder.stop("work"), std::logic_error);
    EXPECT_THROW(static_cast<void>(recorder.result()), std::logic_error);
  }
  {
    TimingRecorder recorder({"work"});
    recorder.begin_measurement(1U);
    recorder.begin_step_measurement();
    recorder.start("work");
    EXPECT_THROW(recorder.start("work"), std::logic_error);
    EXPECT_THROW(static_cast<void>(recorder.result()), std::logic_error);
  }
  {
    TimingRecorder recorder({"work"});
    recorder.begin_measurement(1U);
    recorder.begin_step_measurement();
    recorder.start("work");
    EXPECT_THROW(recorder.end_step_measurement(), std::logic_error);
  }
  {
    TimingRecorder recorder({"work"});
    recorder.begin_measurement(1U);
    EXPECT_THROW(static_cast<void>(recorder.register_section("late")),
                 std::logic_error);
  }
  {
    TimingRecorder recorder({"work"});
    EXPECT_THROW(recorder.start("typo"), std::logic_error);
  }
  {
    TimingRecorder recorder({"work"});
    EXPECT_THROW(recorder.start(TimingRecorder::SectionId{9U}),
                 std::logic_error);
  }
}

TEST(TimingRecorderTest,
     RejectsIncompleteRunsExtraStepsAndModeChangesDuringTiming) {
  EXPECT_THROW(static_cast<void>(TimingRecorder({""})), std::invalid_argument);
  TimingRecorder recorder({"work"});
  EXPECT_THROW(recorder.begin_measurement(0U), std::invalid_argument);
  recorder.begin_measurement(2U);
  recorder.begin_step_measurement();
  recorder.record_zero("work");
  recorder.end_step_measurement();
  EXPECT_THROW(static_cast<void>(recorder.result()), std::logic_error);
  recorder.begin_step_measurement();
  recorder.record_zero("work");
  recorder.end_step_measurement();
  EXPECT_NO_THROW(static_cast<void>(recorder.result()));
  EXPECT_THROW(recorder.begin_step_measurement(), std::logic_error);
  EXPECT_THROW(static_cast<void>(recorder.result()), std::logic_error);

  TimingRecorder mode({"work"});
  mode.begin_measurement(1U);
  mode.begin_step_measurement();
  EXPECT_THROW(mode.set_enabled(false), std::logic_error);
}

TEST(StorageReportTest, CountsPointersAndSeparatelyAllocatedObjectsOnce) {
  struct Object {
    double position[3];
  };
  std::vector<std::unique_ptr<Object>> objects;
  objects.reserve(8U);
  objects.push_back(std::make_unique<Object>());
  objects.push_back(std::make_unique<Object>());
  StorageReport report;
  report.add_vector("pointers", objects);
  report.add_objects<Object>("objects", objects.size());
  EXPECT_TRUE(report.complete());
  EXPECT_EQ(report.size_bytes(),
            2U * sizeof(std::unique_ptr<Object>) + 2U * sizeof(Object));
  EXPECT_EQ(report.capacity_bytes(),
            objects.capacity() * sizeof(std::unique_ptr<Object>) +
                2U * sizeof(Object));
}

TEST(StorageReportTest,
     PreservesZeroAndMissingAndRejectsDoubleCountingOrOverflow) {
  StorageReport report;
  report.add_bytes("empty", 0U, 0U);
  report.add_unavailable("opaque", MeasurementStatus::Unsupported,
                         "Opaque allocation");
  EXPECT_FALSE(report.complete());
  ASSERT_EQ(report.entries().size(), 2U);
  EXPECT_TRUE(report.entries()[0].size_bytes.has_value());
  EXPECT_FALSE(report.entries()[1].size_bytes.has_value());
  EXPECT_EQ(report.entries()[1].reason, "Opaque allocation");
  EXPECT_THROW(report.add_bytes("empty", 0U, 0U), std::invalid_argument);
  EXPECT_THROW(report.add_bytes("bad", 2U, 1U), std::invalid_argument);
  EXPECT_THROW(report.add_unavailable("bad", MeasurementStatus::Available, "x"),
               std::invalid_argument);
  EXPECT_THROW(report.add_objects<double>(
                   "huge", std::numeric_limits<std::size_t>::max()),
               std::overflow_error);
  StorageReport total;
  total.add_bytes("max", 0U, std::numeric_limits<std::size_t>::max());
  EXPECT_THROW(total.add_bytes("overflow", 0U, 1U), std::overflow_error);
}

TEST(ProcessMemoryTest, ReturnsPeaksOrAnExplicitUnavailableStatus) {
  const auto memory = engine::measurement::QueryProcessMemory();
#if defined(_WIN32)
  ASSERT_EQ(memory.status, MeasurementStatus::Available) << memory.reason;
  ASSERT_TRUE(memory.peak_private_commit_bytes.has_value());
  ASSERT_TRUE(memory.peak_working_set_bytes.has_value());
  EXPECT_GT(*memory.peak_working_set_bytes, 0U);
#else
  EXPECT_EQ(memory.status, MeasurementStatus::Unsupported);
  EXPECT_FALSE(memory.peak_private_commit_bytes.has_value());
  EXPECT_FALSE(memory.reason.empty());
#endif
}

}  // namespace
