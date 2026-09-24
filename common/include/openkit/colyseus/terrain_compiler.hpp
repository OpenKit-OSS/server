#pragma once

#include "blueboat/common/json.hpp"

namespace openkit::colyseus {

using Value = blueboat::Value;

class TerrainCompiler {
public:
  static Value decode(const Value &terrain_changes);
  static Value encode(const Value &tiles, int update_id = 0,
                      bool initial = true);
};

} // namespace openkit::colyseus
