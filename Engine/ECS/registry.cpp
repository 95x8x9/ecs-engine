#include "Engine/ECS/registry.h"

namespace engine::ecs {

void Registry::AppendStorageReport(measurement::StorageReport& report) const {
  report.add_vector("registry.alive", alive_entities_);
  report.add_vector("registry.generations", generations_);
  report.add_vector("registry.free_ids", free_entity_ids_);
  using MapEntry = decltype(component_storages_)::value_type;
  report.add_objects<MapEntry>("registry.storage_map.entries",
                               component_storages_.size());
  report.add_unavailable(
      "registry.storage_map.links_and_buckets",
      measurement::MeasurementStatus::Unsupported,
      "std::unordered_map exposes no portable node/bucket allocation size");
  for (const auto& [type, storage] : component_storages_) {
    storage->AppendStorageReport(report,
                                 std::string("components.") + type.name());
  }
}

}  // namespace engine::ecs
