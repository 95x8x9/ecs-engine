#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "Engine/ECS/system.h"
#include "Engine/Measurement/timing_recorder.h"

namespace {
using engine::ecs::Registry;
using engine::ecs::System;
using engine::ecs::SystemManager;
using engine::measurement::StorageReport;
using engine::measurement::TimingRecorder;

struct Trace {
  std::vector<int> order;
  TimingRecorder* expected_recorder{nullptr};
  std::size_t created{0U};
  std::size_t destroyed{0U};
  bool probe_disabled{false};
};

class ProbeSystem final : public System {
 public:
  ProbeSystem(Trace& trace, int id, bool in_group = false)
      : trace_(trace), id_(id), in_group_(in_group) {
    ++trace_.created;
  }
  ~ProbeSystem() override { ++trace_.destroyed; }
  using System::Update;
  void Update(Registry&, float dt, TimingRecorder& timing) override {
    EXPECT_FLOAT_EQ(dt, 0.025F);
    trace_.order.push_back(id_);
    if (trace_.expected_recorder != nullptr) {
      EXPECT_EQ(&timing, trace_.expected_recorder);
      // total_ms rejects reads while an interval is open, without invalidating
      // the recorder. This probes the group boundary inside the actual work.
      if (in_group_) {
        EXPECT_THROW(static_cast<void>(timing.total_ms("group")),
                     std::logic_error);
      } else {
        EXPECT_NO_THROW(static_cast<void>(timing.total_ms("group")));
      }
    }
    if (trace_.probe_disabled) {
      timing.start("not_registered");
      timing.stop("not_registered");
    }
  }

 private:
  Trace& trace_;
  int id_;
  bool in_group_;
};

class AuxiliarySystem final : public System {
 public:
  AuxiliarySystem() {
    values_.reserve(9U);
    values_.resize(2U);
  }
  void Update(Registry&, float, TimingRecorder&) override {}
  void AppendStorageReport(StorageReport& report) const override {
    report.add_vector("auxiliary.values", values_);
  }
  [[nodiscard]] std::size_t Capacity() const { return values_.capacity(); }

 private:
  std::vector<double> values_;
};

const engine::measurement::StorageEntry* Find(const StorageReport& report,
                                              std::string_view name) {
  for (const auto& entry : report.entries()) {
    if (entry.name == name) return &entry;
  }
  return nullptr;
}

TEST(SystemManagerTest,
     ForwardsSameRecorderInOrderAndTimesConsecutiveSystemsOnce) {
  Registry registry;
  Trace trace;
  trace.order.reserve(4U);
  SystemManager manager;
  manager.Add<ProbeSystem>(trace, 0);
  manager.AddTimed<ProbeSystem>("group", trace, 1, true);
  manager.AddTimed<ProbeSystem>("group", trace, 2, true);
  manager.AddTimed<ProbeSystem>("group", trace, 3, true);
  TimingRecorder timing({"group"});
  trace.expected_recorder = &timing;
  timing.begin_measurement(1U);
  timing.begin_step_measurement();
  manager.Update(registry, 0.025F, timing);
  timing.end_step_measurement();

  EXPECT_EQ(trace.order, (std::vector<int>{0, 1, 2, 3}));
  EXPECT_EQ(manager.Size(), 4U);
  const auto result = timing.result();
  ASSERT_EQ(result.sections.size(), 1U);
  EXPECT_EQ(result.sections[0].calls, 1U);
  EXPECT_EQ(result.sections[0].recorded_steps, 1U);
}

TEST(SystemManagerTest, StopsGroupsAroundUntimedSystemsAndRestartsLaterGroups) {
  Registry registry;
  Trace trace;
  trace.order.reserve(5U);
  SystemManager manager;
  manager.AddTimed<ProbeSystem>("group", trace, 0, true);
  manager.AddTimed<ProbeSystem>("group", trace, 1, true);
  manager.Add<ProbeSystem>(trace, 2);
  manager.AddTimed<ProbeSystem>("group", trace, 3, true);
  manager.Add<ProbeSystem>(trace, 4);
  TimingRecorder timing({"group"});
  trace.expected_recorder = &timing;
  timing.begin_measurement(1U);
  timing.begin_step_measurement();
  manager.Update(registry, 0.025F, timing);
  timing.end_step_measurement();

  EXPECT_EQ(trace.order, (std::vector<int>{0, 1, 2, 3, 4}));
  EXPECT_EQ(timing.result().sections[0].calls, 2U);
}

TEST(SystemManagerTest, ChangingSectionClosesPreviousInterval) {
  Registry registry;
  Trace trace;
  SystemManager manager;
  manager.AddTimed<ProbeSystem>("group", trace, 0, true);
  manager.AddTimed<ProbeSystem>("other", trace, 1, true);
  TimingRecorder timing({"group", "other"});
  timing.begin_measurement(1U);
  timing.begin_step_measurement();
  manager.Update(registry, 0.025F, timing);
  timing.end_step_measurement();
  const auto result = timing.result();
  EXPECT_EQ(result.sections[0].calls, 1U);
  EXPECT_EQ(result.sections[1].calls, 1U);
}

TEST(SystemManagerTest, OwnsLabelsAndSupportsUninstrumentedUpdateOverloads) {
  Registry registry;
  Trace trace;
  trace.probe_disabled = true;
  SystemManager manager;
  std::string label = "group";
  auto& system = manager.AddTimed<ProbeSystem>(label, trace, 1, true);
  label.assign("different_name");
  // Both two-argument overloads create a disabled recorder. Even unknown
  // internal section names must be harmless on these paths.
  EXPECT_NO_THROW(system.Update(registry, 0.025F));
  System& base = system;
  EXPECT_NO_THROW(base.Update(registry, 0.025F));
  EXPECT_NO_THROW(manager.Update(registry, 0.025F));
  EXPECT_EQ(trace.order, (std::vector<int>{1, 1, 1}));

  trace.probe_disabled = false;
  TimingRecorder timing({"group"});
  timing.begin_measurement(1U);
  timing.begin_step_measurement();
  manager.Update(registry, 0.025F, timing);
  timing.end_step_measurement();
  EXPECT_EQ(timing.result().sections[0].calls, 1U);
}

TEST(SystemManagerTest, ReportsDerivedObjectsAuxiliaryStorageAndLabelsOnce) {
  Trace trace;
  SystemManager manager;
  manager.Add<ProbeSystem>(trace, 0);
  const auto& auxiliary = manager.AddTimed<AuxiliarySystem>("group");
  StorageReport report;
  manager.AppendStorageReport(report);
  EXPECT_TRUE(report.complete());
  const auto* first = Find(report, "systems.0.object");
  const auto* second = Find(report, "systems.1.object");
  const auto* array = Find(report, "systems.entries");
  const auto* label = Find(report, "systems.1.timing_section");
  const auto* values = Find(report, "auxiliary.values");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(array, nullptr);
  ASSERT_NE(label, nullptr);
  ASSERT_NE(values, nullptr);
  EXPECT_EQ(first->size_bytes, sizeof(ProbeSystem));
  EXPECT_EQ(second->size_bytes, sizeof(AuxiliarySystem));
  EXPECT_EQ(label->size_bytes, 5U);
  EXPECT_EQ(values->size_bytes, 2U * sizeof(double));
  EXPECT_EQ(values->capacity_bytes, auxiliary.Capacity() * sizeof(double));
  EXPECT_EQ(report.size_bytes(), *array->size_bytes + sizeof(ProbeSystem) +
                                     sizeof(AuxiliarySystem) + 5U +
                                     2U * sizeof(double));
}

TEST(SystemManagerTest, RejectsEmptyTimedLabelsAndReleasesSystemsOnClear) {
  Trace trace;
  SystemManager manager;
  EXPECT_THROW(manager.AddTimed<ProbeSystem>("", trace, 0),
               std::invalid_argument);
  EXPECT_EQ(trace.created, 0U);
  EXPECT_EQ(manager.Size(), 0U);
  manager.Add<ProbeSystem>(trace, 1);
  manager.AddTimed<ProbeSystem>("group", trace, 2);
  manager.Clear();
  EXPECT_EQ(trace.destroyed, 2U);
  EXPECT_EQ(manager.Size(), 0U);
  StorageReport report;
  manager.AppendStorageReport(report);
  ASSERT_EQ(report.entries().size(), 1U);
  EXPECT_EQ(report.entries()[0].name, "systems.entries");
  EXPECT_EQ(report.entries()[0].size_bytes, 0U);
  EXPECT_GT(report.entries()[0].capacity_bytes, 0U);
}

}  // namespace
