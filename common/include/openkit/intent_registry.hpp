#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "blueboat/common/json.hpp"

namespace openkit {

using blueboat::Value;

class IntentRegistry {
public:
  std::string register_intent(Value data);
  std::optional<Value> resolve_intent(const std::string &intent_id) const;

  std::string reserve_code();
  void link_code_to_room(const std::string &code, const std::string &room_id);
  std::optional<std::string> resolve_code(const std::string &code) const;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Value> intents_;
  std::unordered_map<std::string, std::string> codes_;
};

} // namespace openkit
