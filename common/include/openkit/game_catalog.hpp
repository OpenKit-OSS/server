#pragma once

#include "blueboat/common/json.hpp"

namespace openkit {

using Value = blueboat::Value;

class GameCatalog {
public:
  static const GameCatalog &instance();

  const Value &props() const { return props_; }
  const Value &items() const { return items_; }
  const Value &devices() const { return devices_; }
  const Value &terrain() const { return terrain_; }

  const Value &device_colliders() const { return device_colliders_; }

private:
  GameCatalog();

  Value props_;
  Value items_;
  Value devices_;
  Value terrain_;
  Value device_colliders_;
};

} // namespace openkit
