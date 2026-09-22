#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "blueboat/room.hpp"
#include "openkit/catalog.hpp"
#include "openkit/intent_registry.hpp"

namespace openkit {

using RoomFactory = std::function<std::unique_ptr<blueboat::Room>(
    Catalog, Value default_questions, Value default_game_options, IntentRegistry &)>;

struct GamemodeInfo {
  std::string name;
  std::string blueboat_room_type;
  RoomFactory make_room;
};

void register_gamemode(GamemodeInfo info);

const GamemodeInfo *find_gamemode(const std::string &name);
std::vector<std::string> gamemode_names();

} // namespace openkit
