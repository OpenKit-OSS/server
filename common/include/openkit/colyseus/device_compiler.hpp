#pragma once

#include "blueboat/common/json.hpp"

namespace openkit::colyseus {

using Value = blueboat::Value;

class DeviceCompiler {
public:
  static Value decode(const Value &added_devices);
  static Value encode(const Value &devices);
};

} // namespace openkit::colyseus
