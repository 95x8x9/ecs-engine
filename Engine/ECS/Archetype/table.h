#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#include "Engine/ECS/entity.h"
#include "Engine/Measurement/memory_measurement.h"

namespace engine::ecs::archetype {

namespace detail {

template <typename Component>
constexpr void ValidateComponent() {
  static_assert(std::is_same_v<Component, std::remove_cvref_t<Component>>,
                "Use an unqualified component type");
  static_assert(std::is_trivially_copyable_v<Component> &&
                    std::is_copy_constructible_v<Component> &&
                    std::is_nothrow_move_assignable_v<Component>,
                "Archetype columns store plain, copyable component data");
  static_assert(!std::is_same_v<Component, bool>,
                "Wrap bool in a component struct to avoid vector<bool>");
}

class IColumn {
 public:
  virtual ~IColumn() = default;
  virtual void Erase(std::size_t row) noexcept = 0;
  virtual void RollBackAppend(std::size_t previous_size) noexcept = 0;
  virtual void AppendStorageReport(measurement::StorageReport& report,
                                   std::string_view prefix) const = 0;
};

template <typename Component>
class ComponentColumn final : public IColumn {
 public:
  std::vector<Component> values;

  void Erase(std::size_t row) noexcept override {
    if (row != values.size() - 1U) {
      values[row] = std::move(values.back());
    }
    values.pop_back();
  }

  void RollBackAppend(std::size_t previous_size) noexcept override {
    if (values.size() > previous_size) values.pop_back();
  }

  void AppendStorageReport(measurement::StorageReport& report,
                           std::string_view prefix) const override {
    const std::string name(prefix);
    report.add_objects<ComponentColumn>(name + ".object", 1U);
    report.add_vector(name + ".values", values);
  }
};

}  // namespace detail

// Columns contain plain value data; component-owned heap allocations are
// outside this storage model. Structural changes invalidate borrowed
// tables/spans.
class Table {
 public:
  Table(const Table&) = delete;
  Table& operator=(const Table&) = delete;
  Table(Table&&) = delete;
  Table& operator=(Table&&) = delete;

  [[nodiscard]] std::size_t Size() const { return entities_.size(); }
  [[nodiscard]] std::span<const Entity> Entities() const { return entities_; }

  template <typename Component>
  [[nodiscard]] std::span<Component> Column() {
    detail::ValidateComponent<Component>();
    return static_cast<detail::ComponentColumn<Component>&>(
               FindColumn(typeid(Component)))
        .values;
  }

  template <typename Component>
  [[nodiscard]] std::span<const Component> Column() const {
    detail::ValidateComponent<Component>();
    return static_cast<const detail::ComponentColumn<Component>&>(
               FindColumn(typeid(Component)))
        .values;
  }

 private:
  friend class Registry;
  Table() = default;

  struct ColumnEntry {
    std::type_index type;
    std::unique_ptr<detail::IColumn> storage;
  };

  template <typename Component>
  void AddColumn() {
    columns_.push_back(
        {typeid(Component),
         std::make_unique<detail::ComponentColumn<Component>>()});
  }

  template <typename... Components>
  void AppendRow(Entity entity, const Components&... values) {
    const auto previous_size = Size();
    entities_.push_back(entity);
    try {
      (static_cast<detail::ComponentColumn<Components>&>(
           FindColumn(typeid(Components)))
           .values.push_back(values),
       ...);
    } catch (...) {
      for (auto& column : columns_) {
        column.storage->RollBackAppend(previous_size);
      }
      entities_.pop_back();
      throw;
    }
  }

  void EraseRow(std::size_t row) noexcept;
  [[nodiscard]] detail::IColumn& FindColumn(std::type_index type);
  [[nodiscard]] const detail::IColumn& FindColumn(std::type_index type) const;
  [[nodiscard]] bool Matches(std::span<const std::type_index> signature) const;
  void AppendStorageReport(measurement::StorageReport& report,
                           std::string_view prefix) const;

  std::vector<Entity> entities_;
  // Sorted by type so component argument order does not affect the signature.
  std::vector<ColumnEntry> columns_;
};

}  // namespace engine::ecs::archetype
