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

private:
  GameCatalog();

  Value props_;
  Value items_;
  Value devices_;
  Value terrain_;
};

} // namespace openkit
