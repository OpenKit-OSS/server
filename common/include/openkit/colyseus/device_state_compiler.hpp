#pragma once

#include "blueboat/common/json.hpp"

namespace openkit::colyseus {

using Value = blueboat::Value;

class DeviceStateCompiler {
public:
  static Value decode(const Value &message);
  static Value encode(const Value &changes,
                      const Value &removed_ids = Value::array(),
                      bool initial = false);
};

} // namespace openkit::colyseus
