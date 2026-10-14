#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace engine::measurement {

enum class MeasurementStatus { Available, NotMeasured, Unsupported, Failed };
[[nodiscard]] std::string_view StatusName(MeasurementStatus status);

struct ProcessMemoryResult {
  MeasurementStatus status{MeasurementStatus::NotMeasured};
  std::string reason{"Process memory was not queried"};
  std::optional<std::size_t> peak_private_commit_bytes;
  std::optional<std::size_t> peak_working_set_bytes;
};

// Query after timing, before report allocation or simulation destruction.
// Peaks cover the process lifetime up to this query, including instrumentation.
[[nodiscard]] ProcessMemoryResult QueryProcessMemory();

struct StorageEntry {
  std::string name;
  std::optional<std::size_t> size_bytes;
  std::optional<std::size_t> capacity_bytes;
  MeasurementStatus status{MeasurementStatus::Available};
  std::string reason;
};

// Element storage estimates exclude allocator overhead and fragmentation.
// Count allocations once; do not add fields already included in sizeof(T).
// Missing allocations make complete() false; sums then represent subtotals.
class StorageReport {
 public:
  void add_bytes(std::string_view name, std::size_t size_bytes,
                 std::size_t capacity_bytes);
  void add_unavailable(std::string_view name, MeasurementStatus status,
                       std::string_view reason);

  template <typename T, typename Allocator>
  void add_vector(std::string_view name,
                  const std::vector<T, Allocator>& values) {
    static_assert(!std::is_same_v<T, bool>, "vector<bool> requires add_bytes");
    add_bytes(name, multiply(values.size(), sizeof(T)),
              multiply(values.capacity(), sizeof(T)));
  }
  template <typename T>
  void add_objects(std::string_view name, std::size_t count) {
    const auto bytes = multiply(count, sizeof(T));
    add_bytes(name, bytes, bytes);
  }
  [[nodiscard]] const std::vector<StorageEntry>& entries() const {
    return entries_;
  }
  [[nodiscard]] std::size_t size_bytes() const { return size_bytes_; }
  [[nodiscard]] std::size_t capacity_bytes() const { return capacity_bytes_; }
  [[nodiscard]] bool complete() const;

 private:
  static std::size_t multiply(std::size_t count, std::size_t size) {
    if (count > std::numeric_limits<std::size_t>::max() / size) {
      throw std::overflow_error("Storage size overflow");
    }
    return count * size;
  }
  void validate_name(std::string_view name) const;
  std::vector<StorageEntry> entries_;
  std::size_t size_bytes_{0U};
  std::size_t capacity_bytes_{0U};
};

}  // namespace engine::measurement
