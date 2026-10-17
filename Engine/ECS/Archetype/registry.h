#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <typeindex>
#include <utility>
#include <vector>

#include "Engine/ECS/Archetype/table.h"

namespace engine::ecs::archetype {

// Registry-local Entity handles reuse the common ID/generation representation.
// No component migration or structural mutation during column iteration.
class Registry {
 public:
  Registry() = default;
  Registry(const Registry&) = delete;
  Registry& operator=(const Registry&) = delete;
  Registry(Registry&&) = delete;
  Registry& operator=(Registry&&) = delete;

  template <typename... Components>
  [[nodiscard]] Entity Create(const Components&... values) {
    // Values may alias a borrowed column or this table's Entity array.
    const std::tuple<Components...> copies(values...);
    auto& table = GetOrCreateTable<Components...>();
    const bool new_id = free_ids_.empty();
    if (new_id) {
      if (locations_.size() >= Entity::kInvalidId) {
        throw std::overflow_error("Archetype Entity IDs exhausted");
      }
      locations_.emplace_back();
    }
    const auto id = new_id ? static_cast<Entity::IdType>(locations_.size() - 1U)
                           : free_ids_.back();
    auto& location = locations_[id];
    const Entity entity{id, location.generation};
    try {
      std::apply(
          [&](const auto&... copied) { table.AppendRow(entity, copied...); },
          copies);
    } catch (...) {
      if (new_id) locations_.pop_back();
      throw;
    }
    if (!new_id) free_ids_.pop_back();
    location.table = &table;
    location.row = table.Size() - 1U;
    ++entity_count_;
    return entity;
  }

  template <typename... Components>
  [[nodiscard]] Table& GetOrCreateTable() {
    const auto signature = Signature<Components...>();
    if (const auto* table = FindTableBySignature(signature)) {
      return const_cast<Table&>(*table);
    }
    auto table = std::unique_ptr<Table>(new Table);
    (table->AddColumn<Components>(), ...);
    std::sort(table->columns_.begin(), table->columns_.end(),
              [](const auto& left, const auto& right) {
                return left.type < right.type;
              });
    tables_.push_back(std::move(table));
    return *tables_.back();
  }

  template <typename... Components>
  [[nodiscard]] const Table* FindTable() const {
    return FindTableBySignature(Signature<Components...>());
  }

  void Destroy(Entity entity);
  [[nodiscard]] bool IsAlive(Entity entity) const;
  [[nodiscard]] std::size_t EntityCount() const { return entity_count_; }
  void Clear();

  // Reports owned allocations only; the caller counts the Registry object (or
  // the larger Simulation object in which it is embedded) exactly once.
  void AppendStorageReport(measurement::StorageReport& report,
                           std::string_view prefix) const;

 private:
  struct Location {
    Table* table{nullptr};
    std::size_t row{0U};
    Entity::GenerationType generation{0U};
  };

  // Max generation is a retired slot and is never reused or wrapped to zero.
  static constexpr auto kRetiredGeneration =
      std::numeric_limits<Entity::GenerationType>::max();

  template <typename... Components>
  [[nodiscard]] static auto Signature() {
    static_assert(sizeof...(Components) > 0U,
                  "An archetype needs at least one component");
    (detail::ValidateComponent<Components>(), ...);
    std::array<std::type_index, sizeof...(Components)> signature{
        std::type_index(typeid(Components))...};
    std::sort(signature.begin(), signature.end());
    if (std::adjacent_find(signature.begin(), signature.end()) !=
        signature.end()) {
      throw std::invalid_argument("Duplicate archetype component type");
    }
    return signature;
  }

  [[nodiscard]] const Table* FindTableBySignature(
      std::span<const std::type_index> signature) const;

  std::vector<std::unique_ptr<Table>> tables_;
  std::vector<Location> locations_;
  std::vector<Entity::IdType> free_ids_;
  std::size_t entity_count_{0U};
};

}  // namespace engine::ecs::archetype
