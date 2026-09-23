#pragma once

#include <string>

#include "blueboat/common/json.hpp"

namespace openkit {

using Value = blueboat::Value;

class MapCatalog {
public:
  static MapCatalog load(const std::string &map_id);

  const std::string &map_id() const { return map_id_; }
  const Value &world_options() const { return world_options_; }
  const Value &world_changes() const { return world_changes_; }
  const Value &terrain_changes() const { return terrain_changes_; }
  const Value &devices_states_changes() const {
    return devices_states_changes_;
  }
  const Value &map_settings() const { return map_settings_; }
  const Value &world() const { return world_; }

  const Value &devices() const { return devices_; }

private:
  MapCatalog(std::string map_id, Value world_options, Value world_changes,
             Value terrain_changes, Value devices_states_changes,
             Value map_settings, Value world, Value devices)
      : map_id_(std::move(map_id)), world_options_(std::move(world_options)),
        world_changes_(std::move(world_changes)),
        terrain_changes_(std::move(terrain_changes)),
        devices_states_changes_(std::move(devices_states_changes)),
        map_settings_(std::move(map_settings)), world_(std::move(world)),
        devices_(std::move(devices)) {}

  std::string map_id_;
  Value world_options_;
  Value world_changes_;
  Value terrain_changes_;
  Value devices_states_changes_;
  Value map_settings_;
  Value world_;
  Value devices_;
};

} // namespace openkit
