#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Engine/ECS/Archetype/registry.h"

namespace {

using engine::ecs::Entity;
using engine::ecs::archetype::Registry;
using engine::ecs::archetype::Table;
using engine::measurement::StorageEntry;
using engine::measurement::StorageReport;

struct alignas(32) Position {
  float x;
  float y{0.0F};
  float z{0.0F};
  std::array<float, 5> padding{};
};

struct Velocity {
  float x;
  float y{0.0F};
  float z{0.0F};
};

struct Acceleration {
  float x;
};

struct Parameters {
  float speed;
};

const StorageEntry& Entry(const StorageReport& report,
                          const std::string& name) {
  for (const auto& entry : report.entries()) {
    if (entry.name == name) return entry;
  }
  throw std::logic_error("Missing storage entry: " + name);
}

TEST(ArchetypeRegistryTest, SharesSignaturesAcrossComponentArgumentOrders) {
  Registry registry;
  const auto first = registry.Create(Position{1.0F}, Velocity{10.0F});
  const auto second = registry.Create(Velocity{20.0F}, Position{2.0F});
  const auto full = registry.Create(Position{3.0F}, Velocity{30.0F},
                                    Acceleration{4.0F}, Parameters{5.0F});
  auto& pair = registry.GetOrCreateTable<Position, Velocity>();
  EXPECT_EQ(&pair, (&registry.GetOrCreateTable<Velocity, Position>()));
  EXPECT_EQ(&pair, (std::as_const(registry).FindTable<Velocity, Position>()));
  ASSERT_EQ(pair.Size(), 2U);
  EXPECT_EQ(pair.Entities()[0], first);
  EXPECT_EQ(pair.Entities()[1], second);
  EXPECT_FLOAT_EQ(pair.Column<Position>()[0].x, 1.0F);
  EXPECT_FLOAT_EQ(pair.Column<Position>()[1].x, 2.0F);
  EXPECT_FLOAT_EQ(pair.Column<Velocity>()[0].x, 10.0F);
  EXPECT_FLOAT_EQ(pair.Column<Velocity>()[1].x, 20.0F);
  auto& other =
      registry.GetOrCreateTable<Parameters, Position, Acceleration, Velocity>();
  ASSERT_EQ(other.Size(), 1U);
  EXPECT_EQ(other.Entities()[0], full);
  EXPECT_FLOAT_EQ(other.Column<Acceleration>()[0].x, 4.0F);
  EXPECT_FLOAT_EQ(other.Column<Parameters>()[0].speed, 5.0F);
  EXPECT_EQ(registry.EntityCount(), 3U);
}

TEST(ArchetypeRegistryTest, FindsAndCreatesEmptyTablesWithoutCreatingEntities) {
  Registry registry;
  EXPECT_EQ((registry.FindTable<Position, Velocity>()), nullptr);
  StorageReport before;
  registry.AppendStorageReport(before, "world");
  EXPECT_EQ(before.size_bytes(), 0U);
  EXPECT_EQ(before.capacity_bytes(), 0U);

  auto& table = registry.GetOrCreateTable<Position, Velocity>();
  EXPECT_EQ(table.Size(), 0U);
  EXPECT_TRUE(table.Entities().empty());
  EXPECT_TRUE(table.Column<Position>().empty());
  EXPECT_TRUE(table.Column<Velocity>().empty());
  EXPECT_EQ(registry.EntityCount(), 0U);
  EXPECT_EQ((registry.FindTable<Position, Velocity>()), &table);
  EXPECT_EQ((registry.FindTable<Position, Acceleration>()), nullptr);

  StorageReport after;
  registry.AppendStorageReport(after, "world");
  for (int repeat = 0; repeat < 10; ++repeat) {
    EXPECT_EQ((&registry.GetOrCreateTable<Velocity, Position>()), &table);
    EXPECT_EQ((registry.FindTable<Velocity, Position>()), &table);
    EXPECT_TRUE(table.Column<Position>().empty());
  }
  StorageReport repeated;
  registry.AppendStorageReport(repeated, "world");
  EXPECT_EQ(after.size_bytes(), repeated.size_bytes());
  EXPECT_EQ(after.capacity_bytes(), repeated.capacity_bytes());
}

TEST(ArchetypeRegistryTest, ExposesAlignedMutableAndConstColumns) {
  Registry registry;
  static_cast<void>(registry.Create(Position{1.0F}, Velocity{2.0F}));
  auto& table = registry.GetOrCreateTable<Position, Velocity>();
  auto positions = table.Column<Position>();
  ASSERT_EQ(positions.size(), 1U);
  EXPECT_EQ(
      reinterpret_cast<std::uintptr_t>(positions.data()) % alignof(Position),
      0U);
  positions[0].x = 9.0F;
  const auto& read_only = std::as_const(table);
  static_assert(std::is_same_v<decltype(read_only.Column<Position>()),
                               std::span<const Position>>);
  EXPECT_FLOAT_EQ(read_only.Column<Position>()[0].x, 9.0F);
  EXPECT_THROW(static_cast<void>(table.Column<Acceleration>()),
               std::logic_error);
  EXPECT_THROW(static_cast<void>(read_only.Column<Acceleration>()),
               std::logic_error);
  EXPECT_EQ(table.Size(), 1U);
}

TEST(ArchetypeRegistryTest, RejectsDuplicateComponentTypesWithoutCreatingRows) {
  Registry registry;
  EXPECT_THROW(
      static_cast<void>(registry.GetOrCreateTable<Position, Position>()),
      std::invalid_argument);
  EXPECT_THROW(static_cast<void>(registry.FindTable<Position, Position>()),
               std::invalid_argument);
  EXPECT_THROW(
      static_cast<void>(registry.Create(Position{1.0F}, Position{2.0F})),
      std::invalid_argument);
  EXPECT_EQ(registry.EntityCount(), 0U);
  EXPECT_EQ((registry.FindTable<Position>()), nullptr);
}

TEST(ArchetypeRegistryTest, RemovesFirstMiddleAndLastRowsAndRepairsLocations) {
  for (const auto removed_row : {0U, 2U, 3U}) {
    SCOPED_TRACE(removed_row);
    Registry registry;
    std::array<Entity, 4> entities;
    for (std::size_t row = 0; row < entities.size(); ++row) {
      const float value = static_cast<float>(row);
      entities[row] = registry.Create(Position{value}, Velocity{value + 10.0F});
    }
    const auto isolated = registry.Create(Acceleration{99.0F});
    registry.Destroy(entities[removed_row]);
    auto& table = registry.GetOrCreateTable<Velocity, Position>();
    ASSERT_EQ(table.Size(), 3U);
    for (std::size_t row = 0; row < table.Size(); ++row) {
      const auto entity = table.Entities()[row];
      EXPECT_TRUE(registry.IsAlive(entity));
      EXPECT_FLOAT_EQ(table.Column<Position>()[row].x,
                      static_cast<float>(entity.Value()));
      EXPECT_FLOAT_EQ(table.Column<Velocity>()[row].x,
                      static_cast<float>(entity.Value()) + 10.0F);
    }
    EXPECT_FALSE(registry.IsAlive(entities[removed_row]));
    if (removed_row != 3U) {
      EXPECT_EQ(table.Entities()[removed_row], entities.back());
      // This Destroy must resolve the last Entity's new row, not its old row.
      registry.Destroy(entities.back());
      EXPECT_FALSE(registry.IsAlive(entities.back()));
      EXPECT_EQ(table.Size(), 2U);
    }
    EXPECT_TRUE(registry.IsAlive(isolated));
    EXPECT_FLOAT_EQ(
        registry.GetOrCreateTable<Acceleration>().Column<Acceleration>()[0].x,
        99.0F);
  }
}

TEST(ArchetypeRegistryTest, ReusesIdsAcrossTablesWithoutRevivingOldHandles) {
  Registry registry;
  const auto old = registry.Create(Position{1.0F});
  registry.Destroy(old);
  registry.Destroy(old);
  registry.Destroy(Entity{});
  registry.Destroy(Entity{999U, 0U});
  const auto replacement = registry.Create(Velocity{7.0F});
  EXPECT_EQ(old.Value(), replacement.Value());
  EXPECT_NE(old.Generation(), replacement.Generation());
  EXPECT_FALSE(registry.IsAlive(old));
  EXPECT_TRUE(registry.IsAlive(replacement));
  registry.Destroy(old);
  EXPECT_EQ(registry.EntityCount(), 1U);
  EXPECT_TRUE(registry.IsAlive(replacement));
  EXPECT_EQ(registry.GetOrCreateTable<Position>().Size(), 0U);
  registry.Destroy(replacement);
  EXPECT_EQ(registry.GetOrCreateTable<Velocity>().Size(), 0U);
  EXPECT_EQ(registry.EntityCount(), 0U);
}

TEST(ArchetypeRegistryTest, ClearPreservesHistoryForLiveAndPreviouslyFreedIds) {
  Registry registry;
  std::vector<Entity> old;
  for (int index = 0; index < 5; ++index) {
    old.push_back(registry.Create(Position{static_cast<float>(index)}));
  }
  registry.Destroy(old[1]);
  registry.Destroy(old[3]);
  registry.Clear();
  EXPECT_EQ(registry.EntityCount(), 0U);
  EXPECT_EQ((registry.FindTable<Position>()), nullptr);
  // Only reuse one slot, then Clear again: the other histories must survive.
  old.push_back(registry.Create(Velocity{1.0F}));
  registry.Clear();
  registry.Clear();
  for (std::size_t index = 0; index < old.size() + 1U; ++index) {
    static_cast<void>(registry.Create(Acceleration{static_cast<float>(index)}));
    for (const auto entity : old) EXPECT_FALSE(registry.IsAlive(entity));
  }
  const auto count = registry.EntityCount();
  for (const auto entity : old) registry.Destroy(entity);
  EXPECT_EQ(registry.EntityCount(), count);
}

TEST(ArchetypeRegistryTest,
     RepeatedBatchChangesPreserveBothSignaturesAndValues) {
  Registry registry;
  std::array<Entity, 5> stable;
  for (std::size_t index = 0; index < stable.size(); ++index) {
    stable[index] =
        registry.Create(Position{static_cast<float>(index)}, Velocity{2.0F},
                        Acceleration{3.0F}, Parameters{4.0F});
  }
  std::array<Entity, 3> changing;
  for (auto& entity : changing) {
    entity = registry.Create(Position{-1.0F}, Velocity{-2.0F});
  }
  for (int step = 0; step < 200; ++step) {
    SCOPED_TRACE(step);
    const auto previous = changing;
    for (const auto entity : changing) registry.Destroy(entity);
    EXPECT_EQ(registry.EntityCount(), stable.size());
    for (auto& entity : changing) {
      entity = registry.Create(Velocity{static_cast<float>(step)},
                               Position{-static_cast<float>(step)});
    }
    for (const auto entity : previous) EXPECT_FALSE(registry.IsAlive(entity));
    for (const auto entity : changing) EXPECT_TRUE(registry.IsAlive(entity));
    const auto* pair = registry.FindTable<Position, Velocity>();
    ASSERT_NE(pair, nullptr);
    ASSERT_EQ(pair->Size(), changing.size());
    for (std::size_t row = 0; row < pair->Size(); ++row) {
      EXPECT_FLOAT_EQ(pair->Column<Velocity>()[row].x,
                      static_cast<float>(step));
      EXPECT_FLOAT_EQ(pair->Column<Position>()[row].x,
                      -static_cast<float>(step));
    }
    const auto* full =
        registry.FindTable<Position, Velocity, Acceleration, Parameters>();
    ASSERT_NE(full, nullptr);
    ASSERT_EQ(full->Size(), stable.size());
    for (std::size_t row = 0; row < full->Size(); ++row) {
      EXPECT_EQ(full->Entities()[row], stable[row]);
      EXPECT_FLOAT_EQ(full->Column<Position>()[row].x, static_cast<float>(row));
      EXPECT_FLOAT_EQ(full->Column<Parameters>()[row].speed, 4.0F);
    }
    EXPECT_EQ(registry.EntityCount(), stable.size() + changing.size());
  }
}

TEST(ArchetypeRegistryTest, CopiesInputsBeforeGrowingBorrowedEntityArrays) {
  Registry registry;
  const auto first = registry.Create(Entity{123U, 9U});
  for (int index = 0; index < 50; ++index) {
    const auto& table = registry.GetOrCreateTable<Entity>();
    // The argument aliases the array to which Create will append an Entity.
    static_cast<void>(registry.Create(table.Entities()[0]));
  }
  const auto& table = registry.GetOrCreateTable<Entity>();
  EXPECT_EQ(table.Column<Entity>()[0], (Entity{123U, 9U}));
  for (std::size_t row = 1; row < table.Size(); ++row) {
    EXPECT_EQ(table.Column<Entity>()[row], first);
  }
}

TEST(ArchetypeRegistryTest,
     ReportsAllOwnedStorageWithoutCountingEmbeddedFields) {
  Registry registry;
  const auto first = registry.Create(Position{1.0F}, Velocity{2.0F});
  static_cast<void>(registry.Create(Position{3.0F}, Velocity{4.0F}));
  static_cast<void>(registry.Create(Acceleration{5.0F}));
  StorageReport report;
  report.add_objects<Registry>("root", 1U);
  registry.AppendStorageReport(report, "world");
  EXPECT_TRUE(report.complete());
  EXPECT_EQ(Entry(report, "root").size_bytes, sizeof(Registry));
  EXPECT_EQ(Entry(report, "world.tables").size_bytes,
            2U * sizeof(std::unique_ptr<Table>));
  EXPECT_EQ(Entry(report, "world.tables.0.object").size_bytes, sizeof(Table));
  EXPECT_EQ(Entry(report, "world.tables.0.entities").size_bytes,
            2U * sizeof(Entity));
  const std::string positions = std::string("world.tables.0.columns.") +
                                typeid(Position).name() + ".values";
  const std::string velocities = std::string("world.tables.0.columns.") +
                                 typeid(Velocity).name() + ".values";
  EXPECT_EQ(Entry(report, positions).size_bytes, 2U * sizeof(Position));
  EXPECT_EQ(Entry(report, velocities).size_bytes, 2U * sizeof(Velocity));
  for (const auto& entry : report.entries()) {
    ASSERT_TRUE(entry.size_bytes.has_value());
    ASSERT_TRUE(entry.capacity_bytes.has_value());
    EXPECT_GE(*entry.capacity_bytes, *entry.size_bytes);
  }
  registry.Destroy(first);
  StorageReport reduced;
  registry.AppendStorageReport(reduced, "world");
  EXPECT_EQ(Entry(reduced, positions).size_bytes, sizeof(Position));
  EXPECT_EQ(Entry(reduced, positions).capacity_bytes,
            Entry(report, positions).capacity_bytes);
  EXPECT_EQ(Entry(reduced, "world.free_ids").size_bytes,
            sizeof(Entity::IdType));

  registry.Clear();
  StorageReport cleared;
  registry.AppendStorageReport(cleared, "world");
  EXPECT_TRUE(cleared.complete());
  EXPECT_EQ(cleared.entries().size(), 3U);
  EXPECT_EQ(Entry(cleared, "world.tables").size_bytes, 0U);
  EXPECT_EQ(Entry(cleared, "world.locations").size_bytes,
            Entry(report, "world.locations").size_bytes);
  EXPECT_EQ(Entry(cleared, "world.free_ids").size_bytes,
            3U * sizeof(Entity::IdType));
}

TEST(ArchetypeRegistryTest, StorageReportPrefixesKeepRegistriesSeparate) {
  Registry first;
  Registry second;
  static_cast<void>(first.Create(Position{1.0F}));
  static_cast<void>(second.Create(Position{2.0F}));
  StorageReport report;
  first.AppendStorageReport(report, "first");
  second.AppendStorageReport(report, "second");
  EXPECT_TRUE(report.complete());
  EXPECT_EQ(Entry(report, "first.tables.0.entities").size_bytes,
            sizeof(Entity));
  EXPECT_EQ(Entry(report, "second.tables.0.entities").size_bytes,
            sizeof(Entity));
}

}  // namespace
