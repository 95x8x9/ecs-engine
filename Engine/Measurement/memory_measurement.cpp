#include "Engine/Measurement/memory_measurement.h"

#include <algorithm>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off: psapi.h requires Windows types to be declared first.
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif

namespace engine::measurement {

std::string_view StatusName(MeasurementStatus status) {
  switch (status) {
    case MeasurementStatus::Available:
      return "available";
    case MeasurementStatus::NotMeasured:
      return "not_measured";
    case MeasurementStatus::Unsupported:
      return "unsupported";
    case MeasurementStatus::Failed:
      return "failed";
  }
  return "unknown";
}

ProcessMemoryResult QueryProcessMemory() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters{};
  counters.cb = sizeof(counters);
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
    return {.status = MeasurementStatus::Failed,
            .reason = "GetProcessMemoryInfo failed: " +
                      std::to_string(GetLastError())};
  }
  return {.status = MeasurementStatus::Available,
          .reason = {},
          .peak_private_commit_bytes = counters.PeakPagefileUsage,
          .peak_working_set_bytes = counters.PeakWorkingSetSize};
#else
  return {.status = MeasurementStatus::Unsupported,
          .reason = "Process peak collection is implemented for Windows only"};
#endif
}

void StorageReport::validate_name(std::string_view name) const {
  if (name.empty())
    throw std::invalid_argument("Storage name must not be empty");
  for (const auto& entry : entries_) {
    if (entry.name == name)
      throw std::invalid_argument("Duplicate storage entry");
  }
}

void StorageReport::add_bytes(std::string_view name, std::size_t size_bytes,
                              std::size_t capacity_bytes) {
  validate_name(name);
  if (size_bytes > capacity_bytes) {
    throw std::invalid_argument("Storage size exceeds capacity");
  }
  if (size_bytes > std::numeric_limits<std::size_t>::max() - size_bytes_ ||
      capacity_bytes >
          std::numeric_limits<std::size_t>::max() - capacity_bytes_) {
    throw std::overflow_error("Storage total overflow");
  }
  entries_.push_back({std::string(name),
                      size_bytes,
                      capacity_bytes,
                      MeasurementStatus::Available,
                      {}});
  size_bytes_ += size_bytes;
  capacity_bytes_ += capacity_bytes;
}

void StorageReport::add_unavailable(std::string_view name,
                                    MeasurementStatus status,
                                    std::string_view reason) {
  validate_name(name);
  if (status == MeasurementStatus::Available || reason.empty()) {
    throw std::invalid_argument(
        "Unavailable storage needs a status and reason");
  }
  entries_.push_back({std::string(name), std::nullopt, std::nullopt, status,
                      std::string(reason)});
}

bool StorageReport::complete() const {
  return std::all_of(entries_.begin(), entries_.end(),
                     [](const StorageEntry& entry) {
                       return entry.status == MeasurementStatus::Available;
                     });
}

}  // namespace engine::measurement
