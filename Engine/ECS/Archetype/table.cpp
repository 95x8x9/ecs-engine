#include "Engine/ECS/Archetype/table.h"

#include <algorithm>

namespace engine::ecs::archetype {

detail::IColumn& Table::FindColumn(std::type_index type) {
  return const_cast<detail::IColumn&>(std::as_const(*this).FindColumn(type));
}

const detail::IColumn& Table::FindColumn(std::type_index type) const {
  for (const auto& column : columns_) {
    if (column.type == type) return *column.storage;
  }
  throw std::logic_error("Component does not belong to this archetype");
}

bool Table::Matches(std::span<const std::type_index> signature) const {
  return signature.size() == columns_.size() &&
         std::equal(signature.begin(), signature.end(), columns_.begin(),
                    [](std::type_index type, const ColumnEntry& column) {
                      return type == column.type;
                    });
}

void Table::EraseRow(std::size_t row) noexcept {
  for (auto& column : columns_) column.storage->Erase(row);
  if (row != Size() - 1U) entities_[row] = entities_.back();
  entities_.pop_back();
}

void Table::AppendStorageReport(measurement::StorageReport& report,
                                std::string_view prefix) const {
  const std::string name(prefix);
  report.add_objects<Table>(name + ".object", 1U);
  report.add_vector(name + ".entities", entities_);
  report.add_vector(name + ".column_entries", columns_);
  for (const auto& column : columns_) {
    column.storage->AppendStorageReport(
        report, name + ".columns." + column.type.name());
  }
}

}  // namespace engine::ecs::archetype
