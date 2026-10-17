#include "Engine/ECS/Archetype/registry.h"

#include <string>

namespace engine::ecs::archetype {

const Table* Registry::FindTableBySignature(
    std::span<const std::type_index> signature) const {
  for (const auto& table : tables_) {
    if (table->Matches(signature)) return table.get();
  }
  return nullptr;
}

bool Registry::IsAlive(Entity entity) const {
  return entity.IsValid() && entity.Value() < locations_.size() &&
         locations_[entity.Value()].table != nullptr &&
         locations_[entity.Value()].generation == entity.Generation();
}

void Registry::Destroy(Entity entity) {
  if (!IsAlive(entity)) return;
  auto& location = locations_[entity.Value()];
  const auto next_generation = location.generation + 1U;
  // Allocate before changing rows, so allocation failure leaves the entity
  // live.
  if (next_generation != kRetiredGeneration) {
    free_ids_.push_back(entity.Value());
  }
  auto& table = *location.table;
  const auto row = location.row;
  const Entity moved = table.Entities().back();
  table.EraseRow(row);
  if (row < table.Size()) locations_[moved.Value()].row = row;
  location.table = nullptr;
  location.row = 0U;
  location.generation = next_generation;
  --entity_count_;
}

void Registry::Clear() {
  // Keep generation history, including IDs destroyed before Clear().
  free_ids_.reserve(locations_.size());
  free_ids_.clear();
  for (std::size_t id = 0; id < locations_.size(); ++id) {
    auto& location = locations_[id];
    if (location.table != nullptr) ++location.generation;
    location.table = nullptr;
    location.row = 0U;
    if (location.generation != kRetiredGeneration) {
      free_ids_.push_back(static_cast<Entity::IdType>(id));
    }
  }
  tables_.clear();
  entity_count_ = 0U;
}

void Registry::AppendStorageReport(measurement::StorageReport& report,
                                   std::string_view prefix) const {
  const std::string name(prefix);
  report.add_vector(name + ".tables", tables_);
  report.add_vector(name + ".locations", locations_);
  report.add_vector(name + ".free_ids", free_ids_);
  for (std::size_t index = 0; index < tables_.size(); ++index) {
    tables_[index]->AppendStorageReport(
        report, name + ".tables." + std::to_string(index));
  }
}

}  // namespace engine::ecs::archetype
