#include "openkit/gamemode_registry.hpp"

#include <unordered_map>

namespace openkit {

namespace {
std::unordered_map<std::string, GamemodeInfo> &registry() {
  static std::unordered_map<std::string, GamemodeInfo> instance;
  return instance;
}
} // namespace

void register_gamemode(GamemodeInfo info) {
  std::string name = info.name;
  registry().insert_or_assign(name, std::move(info));
}

const GamemodeInfo *find_gamemode(const std::string &name) {
  auto it = registry().find(name);
  return it == registry().end() ? nullptr : &it->second;
}

std::vector<std::string> gamemode_names() {
  std::vector<std::string> names;
  names.reserve(registry().size());
  for (const auto &[name, info] : registry()) {
    names.push_back(name);
  }
  return names;
}

} // namespace openkit
