#include "openkit/intent_registry.hpp"

#include <random>

namespace openkit {

namespace {
std::string random_hex(std::size_t length) {
  static thread_local std::mt19937 rng(std::random_device{}());
  static const char hex_digits[] = "0123456789abcdef";
  std::uniform_int_distribution<int> dist(0, 15);
  std::string out(length, '0');
  for (auto &c : out) {
    c = hex_digits[dist(rng)];
  }
  return out;
}
} // namespace

std::string IntentRegistry::register_intent(Value data) {
  std::string intent_id = random_hex(24);
  std::lock_guard<std::mutex> lock(mutex_);
  intents_[intent_id] = std::move(data);
  return intent_id;
}

std::optional<Value> IntentRegistry::resolve_intent(const std::string &intent_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = intents_.find(intent_id);
  if (it == intents_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::string IntentRegistry::reserve_code() {
  static thread_local std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> dist(10000000, 99999999);

  std::lock_guard<std::mutex> lock(mutex_);
  std::string code;
  do {
    code = std::to_string(dist(rng));
  } while (codes_.count(code));
  codes_[code] = "";
  return code;
}

void IntentRegistry::link_code_to_room(const std::string &code, const std::string &room_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  codes_[code] = room_id;
}

std::optional<std::string> IntentRegistry::resolve_code(const std::string &code) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = codes_.find(code);
  if (it == codes_.end() || it->second.empty()) {
    return std::nullopt;
  }
  return it->second;
}

} // namespace openkit
