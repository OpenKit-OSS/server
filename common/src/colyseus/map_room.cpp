#include "openkit/colyseus/map_room.hpp"

#include <chrono>
#include <unordered_map>
#include <utility>
#include <vector>

#include "openkit/colyseus/auth_token.hpp"
#include "openkit/colyseus/device_state_compiler.hpp"

namespace openkit::colyseus {

namespace {

enum RootField {
  kRoot_Session = 0,
  kRoot_Teams = 1,
  kRoot_World = 2,
  kRoot_Characters = 3,
  kRoot_MapSettings = 4,
  kRoot_CustomAssets = 5,
  kRoot_Matchmaker = 6,
  kRoot_Hooks = 7
};
enum SessionField {
  kSession_Version = 0,
  kSession_ModeType = 1,
  kSession_GameOwnerId = 2,
  kSession_GameTime = 3,
  kSession_PhaseChangedAt = 4,
  kSession_Phase = 5,
  kSession_LoadingPhase = 6,
  kSession_GameSession = 7,
  kSession_GlobalPermissions = 8,
  kSession_MapCreatorRoleLevel = 9,
  kSession_CosmosBlocked = 10,
  kSession_AllowGoogleTranslate = 11,
  kSession_MapStyle = 12
};
enum GameSessionField {
  kGameSession_Phase = 0,
  kGameSession_CountdownEnd = 1,
  kGameSession_ResultsEnd = 2,
  kGameSession_CallToAction = 3
};
enum CallToActionField { kCTA_Categories = 0, kCTA_Items = 1 };
enum PermissionsField {
  kPerms_Adding = 0,
  kPerms_Removing = 1,
  kPerms_Editing = 2,
  kPerms_ManageCodeGrids = 3
};
enum TeamField {
  kTeam_Id = 0,
  kTeam_Name = 1,
  kTeam_Score = 2,
  kTeam_Characters = 3
};
enum WorldField { kWorld_Width = 0, kWorld_Height = 1, kWorld_Devices = 2 };
enum WorldDevicesField { kWorldDevices_CodeGrids = 0 };
enum CharacterField {
  kChar_Id = 0,
  kChar_TeamId = 1,
  kChar_LastPlayersTeamId = 2,
  kChar_Name = 3,
  kChar_Type = 4,
  kChar_IsActive = 5,
  kChar_RoleLevel = 6,
  kChar_X = 7,
  kChar_Y = 8,
  kChar_Scale = 9,
  kChar_TeleportCount = 10,
  kChar_MovementSpeed = 11,
  kChar_Phase = 12,
  kChar_OpenDeviceUI = 13,
  kChar_OpenDeviceUIChangeCounter = 14,
  kChar_CompletedInitialPlacement = 15,
  kChar_IsRespawning = 16,
  kChar_Score = 17,
  kChar_ClassDesigner = 18,
  kChar_Appearance = 19,
  kChar_Permissions = 20,
  kChar_Inventory = 21,
  kChar_Xp = 22,
  kChar_Assignment = 23,
  kChar_Health = 24,
  kChar_Projectiles = 25,
  kChar_ZoneAbilitiesOverrides = 26,
  kChar_Physics = 27,
};
enum InventoryField {
  kInv_Slots = 0,
  kInv_MaxSlots = 1,
  kInv_ActiveInteractiveSlot = 2,
  kInv_InteractiveSlots = 3,
  kInv_InteractiveSlotsOrder = 4,
  kInv_InfiniteAmmo = 5
};
enum InteractiveSlotField {
  kSlot_ItemId = 0,
  kSlot_Waiting = 1,
  kSlot_WaitingStartTime = 2,
  kSlot_WaitingEndTime = 3,
  kSlot_CurrentClip = 4,
  kSlot_ClipSize = 5,
  kSlot_Durability = 6,
  kSlot_Count = 7
};
enum HealthField {
  kHealth_Fragility = 0,
  kHealth_Health = 1,
  kHealth_Shield = 2,
  kHealth_MaxHealth = 3,
  kHealth_MaxShield = 4,
  kHealth_Lives = 5,
  kHealth_SpawnImmunityActive = 6,
  kHealth_ClassImmunityActive = 7,
  kHealth_ShowHealthBar = 8
};
enum ZoneField {
  kZone_AllowWeaponFire = 0,
  kZone_AllowWeaponDrop = 1,
  kZone_AllowItemDrop = 2,
  kZone_AllowResourceDrop = 3
};
enum PhysicsField { kPhysics_IsGrounded = 0, kPhysics_IsWallSliding = 1 };
enum ProjectilesField { kProj_AimAngle = 0, kProj_DamageMultiplier = 1 };
enum AppearanceField {
  kAppearance_Skin = 0,
  kAppearance_TrailId = 1,
  kAppearance_TransparencyModifierId = 2,
  kAppearance_TintModifierId = 3
};
enum ClassDesignerField {
  kClassDesigner_LastActivatedClassDeviceId = 0,
  kClassDesigner_LastClassDeviceActivationId = 1
};
enum InventorySlotField { kInvSlot_Amount = 0 };
enum MatchmakerField { kMatchmaker_GameCode = 0 };
enum HooksField { kHooks_HookJSON = 0 };

int default_clip_size(const std::string &item_id) {
  if (item_id.rfind("snowball_launcher_", 0) == 0)
    return 16;
  return 0;
}

constexpr int kRootClass = 0;

Value default_memory_costs_and_limits() {
  return Value::array({100000, 500, 3, 10, 10, 2, 10, 999999999999LL,
                       999999999999LL, 2500, 5000, 999999, 999999999999LL, 0,
                       75, 6});
}

double now_ms() {
  return static_cast<double>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

constexpr auto kTickInterval = std::chrono::milliseconds(50);

} // namespace

void MapRoom::on_create(const Value &options) {
  std::string intent_id = options.value("intentId", std::string());
  std::string game_code;
  if (!intent_id.empty()) {
    if (auto intent = intent_registry_.resolve_intent(intent_id)) {
      game_options_ = intent->value("gameOptions", Value::object());
      game_code = intent->value("gameCode", std::string());
    }
  }

  init_schema_state(kRootClass);
  schema::Node &root = state();

  auto session = root.ref_child(kRoot_Session);
  session->set_string(kSession_Version, "published");
  session->set_string(kSession_ModeType, "liveGame");
  std::string owner_id =
      decode_user_id_from_auth_token(options.value("authToken", std::string()))
          .value_or(std::string());
  session->set_string(kSession_GameOwnerId, owner_id);
  session->set_number(kSession_GameTime, 0);
  session->set_number(kSession_PhaseChangedAt, 0);
  session->set_string(kSession_Phase, "preGame");
  session->set_bool(kSession_LoadingPhase, false);
  auto game_session = session->ref_child(kSession_GameSession);
  game_session->set_string(kGameSession_Phase, "countdown");
  game_session->set_number(kGameSession_CountdownEnd, 0);
  game_session->set_number(kGameSession_ResultsEnd, 0);
  auto call_to_action = game_session->ref_child(kGameSession_CallToAction);
  call_to_action->array_child(kCTA_Categories);
  call_to_action->array_child(kCTA_Items);
  auto global_perms = session->ref_child(kSession_GlobalPermissions);
  global_perms->set_bool(kPerms_Adding, false);
  global_perms->set_bool(kPerms_Removing, false);
  global_perms->set_bool(kPerms_Editing, false);
  global_perms->set_bool(kPerms_ManageCodeGrids, false);
  session->set_number(kSession_MapCreatorRoleLevel, 90);
  session->set_bool(kSession_CosmosBlocked,
                    options.value("cosmosBlocked", false));
  session->set_bool(kSession_AllowGoogleTranslate,
                    options.value("allowGoogleTranslate", true));
  session->set_string(kSession_MapStyle, "topDown");

  auto teams = root.array_child(kRoot_Teams);
  for (int i = 1; i <= 8; i++) {
    auto team = teams->push_ref();
    team->set_string(kTeam_Id, std::to_string(i));
    team->set_string(kTeam_Name, "Team " + std::to_string(i));
    team->set_number(kTeam_Score, 0);
    team->array_child(kTeam_Characters);
  }

  auto world = root.ref_child(kRoot_World);
  world->set_number(kWorld_Width, map_catalog_.world().value("width", 0));
  world->set_number(kWorld_Height, map_catalog_.world().value("height", 0));
  world->ref_child(kWorld_Devices)->map_child(kWorldDevices_CodeGrids);

  Value map_settings = map_catalog_.map_settings();
  Value hook_options = game_options_.value("hookOptions", Value::object());
  if (hook_options.contains("gameDuration")) {
    map_settings["countdownTimeMinutes"] = hook_options.at("gameDuration");
  }
  if (hook_options.contains("teams")) {
    map_settings["teams"] = hook_options.at("teams");
  }
  game_settings_ = map_settings;
  root.set_string(kRoot_MapSettings, map_settings.dump());
  root.map_child(kRoot_CustomAssets);
  root.ref_child(kRoot_Matchmaker)->set_string(kMatchmaker_GameCode, game_code);
  root.ref_child(kRoot_Hooks)->set_string(kHooks_HookJSON, "{}");

  schedule_tick();
}

void MapRoom::schedule_tick() {
  tick_timer_ =
      blueboat::Scheduler::instance().set_timeout(kTickInterval, [this] {
        std::lock_guard<std::recursive_mutex> guard(mutex());
        state()
            .ref_child(kRoot_Session)
            ->set_number(kSession_GameTime, now_ms());
        broadcast_state_patch();
        schedule_tick();
      });
}

void MapRoom::on_dispose() { tick_timer_.clear(); }

void MapRoom::on_join(Client &client, const Value &options) {
  auto characters = state().map_child(kRoot_Characters);
  auto character = characters->get_or_create(client.id());
  character->set_string(kChar_Id, client.id());
  character->set_string(kChar_TeamId, "__NO_TEAM_ID");
  character->set_string(kChar_LastPlayersTeamId, "");
  character->set_string(kChar_Name, options.value("name", client.id()));
  character->set_string(kChar_Type, "player");
  character->set_bool(kChar_IsActive, true);
  character->set_number(kChar_RoleLevel, 50);
  character->set_number(kChar_X, 6500);
  character->set_number(kChar_Y, 6000);
  character->set_number(kChar_Scale, 1);
  character->set_number(kChar_TeleportCount, 0);
  character->set_number(kChar_MovementSpeed, 310);
  character->set_bool(kChar_Phase, false);
  character->set_string(kChar_OpenDeviceUI, "");
  character->set_number(kChar_OpenDeviceUIChangeCounter, 0);
  character->set_bool(kChar_CompletedInitialPlacement, true);
  character->set_bool(kChar_IsRespawning, false);
  character->set_number(kChar_Score, 0);
  auto class_designer = character->ref_child(kChar_ClassDesigner);
  class_designer->set_string(kClassDesigner_LastActivatedClassDeviceId, "");
  class_designer->set_number(kClassDesigner_LastClassDeviceActivationId, 1);
  auto appearance = character->ref_child(kChar_Appearance);
  appearance->set_string(kAppearance_Skin, R"({"id":"character_truffle"})");
  appearance->set_string(kAppearance_TrailId, "");
  appearance->set_string(kAppearance_TransparencyModifierId, "");
  appearance->set_string(kAppearance_TintModifierId, "");
  auto permissions = character->ref_child(kChar_Permissions);
  permissions->set_bool(kPerms_Adding, false);
  permissions->set_bool(kPerms_Removing, false);
  permissions->set_bool(kPerms_Editing, false);
  permissions->set_bool(kPerms_ManageCodeGrids, false);
  auto inventory = character->ref_child(kChar_Inventory);
  inventory->map_child(kInv_Slots);
  inventory->set_number(kInv_MaxSlots, 999);
  inventory->set_number(kInv_ActiveInteractiveSlot, 0);
  constexpr int kInteractiveSlotCount = 5;
  auto interactive_slots = inventory->map_child(kInv_InteractiveSlots);
  auto interactive_slots_order =
      inventory->array_child(kInv_InteractiveSlotsOrder);
  for (int i = 1; i <= kInteractiveSlotCount; i++) {
    auto slot = interactive_slots->get_or_create(std::to_string(i));
    slot->set_string(kSlot_ItemId, "");
    slot->set_bool(kSlot_Waiting, false);
    slot->set_number(kSlot_WaitingStartTime, 0);
    slot->set_number(kSlot_WaitingEndTime, 0);
    slot->set_number(kSlot_CurrentClip, 0);
    slot->set_number(kSlot_ClipSize, 0);
    slot->set_number(kSlot_Durability, -1);
    slot->set_number(kSlot_Count, 0);
    interactive_slots_order->push_primitive(Value(i));
  }
  inventory->set_bool(kInv_InfiniteAmmo, false);
  character->ref_child(kChar_Xp);
  character->ref_child(kChar_Assignment);
  auto health = character->ref_child(kChar_Health);
  health->set_number(kHealth_Fragility,
                     game_settings_.value("startingFragility", 0.0));
  health->set_number(kHealth_Health,
                     game_settings_.value("startingHealth", 100.0));
  health->set_number(kHealth_Shield,
                     game_settings_.value("startingShield", 0.0));
  health->set_number(kHealth_MaxHealth,
                     game_settings_.value("maxHealth", 100.0));
  health->set_number(kHealth_MaxShield, game_settings_.value("maxShield", 0.0));
  health->set_number(kHealth_Lives,
                     game_settings_.value("useInfiniteLives", false)
                         ? -1.0
                         : game_settings_.value("numOfLives", 3.0));
  health->set_bool(kHealth_SpawnImmunityActive, false);
  health->set_bool(kHealth_ClassImmunityActive, false);
  health->set_bool(kHealth_ShowHealthBar, false);
  character->ref_child(kChar_Projectiles);
  auto zone = character->ref_child(kChar_ZoneAbilitiesOverrides);
  zone->set_bool(kZone_AllowWeaponFire, true);
  zone->set_bool(kZone_AllowWeaponDrop, true);
  zone->set_bool(kZone_AllowItemDrop, true);
  zone->set_bool(kZone_AllowResourceDrop, true);
  auto physics = character->ref_child(kChar_Physics);
  physics->set_bool(kPhysics_IsGrounded, false);
  physics->set_bool(kPhysics_IsWallSliding, false);

  grant_starting_inventory(character);

  broadcast_state_patch();

  client.send("AUTH_ID", client.id());
  client.send("MY_TEAM", "__NO_TEAM_ID");
  client.send("MEMORY_COSTS_AND_LIMITS", default_memory_costs_and_limits());
  client.send("INFO_BEFORE_WORLD_SYNC",
              Value{{"x", character->get_number(kChar_X)},
                    {"y", character->get_number(kChar_Y)}});
  client.send("WORLD_OPTIONS", map_catalog_.world_options());
}

void MapRoom::on_message(Client &client, const std::string &type,
                         const Value &data) {
  if (type == "REQUEST_INITIAL_WORLD") {
    handle_request_initial_world(client);
  } else if (type == "INPUT") {
    handle_input(client, data);
  } else if (type == "START_GAME") {
    handle_start_game(client, data);
  } else if (type == "SET_ACTIVE_INTERACTIVE_ITEM") {
    handle_set_active_interactive_item(client, data);
  } else if (type == "AIMING") {
    handle_aiming(client, data);
  }
}

void MapRoom::on_leave(Client &client, bool /*intentional*/) {
  state().map_child(kRoot_Characters)->erase(client.id());
  broadcast_state_patch();
}

void MapRoom::handle_request_initial_world(Client &client) {
  client.send("WORLD_CHANGES", map_catalog_.world_changes());
  client.send("TERRAIN_CHANGES", map_catalog_.terrain_changes());
  client.send("DEVICES_STATES_CHANGES", map_catalog_.devices_states_changes());
}

void MapRoom::handle_input(Client &client, const Value &data) {
  auto characters = state().map_child(kRoot_Characters);
  auto character = characters->find(client.id());
  if (!character)
    return;

  double x = data.value("x", character->get_number(kChar_X) / 100.0) * 100.0;
  double y = data.value("y", character->get_number(kChar_Y) / 100.0) * 100.0;
  character->set_number(kChar_X, x);
  character->set_number(kChar_Y, y);
  Value physics_state{
      {"actualMovement", Value{{"x", 0}, {"y", 0}}},
      {"movement", Value{{"direction", "none"},
                         {"xVelocity", 0},
                         {"accelerationTicks", 0}}},
      {"velocity", Value{{"x", 0}, {"y", 0}}},
      {"grounded", true},
  };
  client.send(
      "PHYSICS_STATE",
      Value{{"x", x}, {"y", y}, {"physicsState", physics_state.dump()}});
}

void MapRoom::handle_set_active_interactive_item(Client &client,
                                                 const Value &data) {
  auto character = state().map_child(kRoot_Characters)->find(client.id());
  if (!character)
    return;

  int slot_num = static_cast<int>(data.value("slotNum", 0.0));
  auto inventory = character->ref_child(kChar_Inventory);
  inventory->set_number(kInv_ActiveInteractiveSlot, slot_num);

  constexpr double kWeaponSwitchDelayMs = 1800;
  auto slot = inventory->map_child(kInv_InteractiveSlots)
                  ->find(std::to_string(slot_num));
  if (slot) {
    double now = now_ms();
    slot->set_number(kSlot_WaitingStartTime, now);
    slot->set_number(kSlot_WaitingEndTime, now + kWeaponSwitchDelayMs);
  }

  broadcast_state_patch();
}

void MapRoom::handle_aiming(Client &client, const Value &data) {
  auto character = state().map_child(kRoot_Characters)->find(client.id());
  if (!character)
    return;
  character->ref_child(kChar_Projectiles)
      ->set_number(kProj_AimAngle, data.value("angle", 0.0));
  broadcast_state_patch();
}

void MapRoom::assign_teams(const std::string &owner_id, bool owner_as_spectator,
                           const Value &custom_teams) {
  std::string team_mode =
      game_settings_.value("teams", std::string("Free For All"));
  if (team_mode == "Free For All")
    return;

  int teams_number = static_cast<int>(game_settings_.value("teamsNumber", 2.0));
  if (teams_number < 1)
    teams_number = 1;

  auto teams = state().array_child(kRoot_Teams);
  auto characters = state().map_child(kRoot_Characters);
  if (teams_number > static_cast<int>(teams->entries().size()))
    teams_number = static_cast<int>(teams->entries().size());

  int next_team = 0;
  for (const auto &entry : characters->entries()) {
    if (!entry.alive)
      continue;
    const std::string &char_id = entry.key;
    if (owner_as_spectator && char_id == owner_id)
      continue;

    std::string team_id;
    if (custom_teams.contains(char_id) &&
        custom_teams.at(char_id).is_string()) {
      team_id = custom_teams.at(char_id).get<std::string>();
    } else {
      team_id = std::to_string((next_team % teams_number) + 1);
      next_team++;
    }

    int idx = 0;
    try {
      idx = std::stoi(team_id);
    } catch (...) {
      continue;
    }
    if (idx < 1 || idx > static_cast<int>(teams->entries().size()))
      continue;

    entry.value->set_string(kChar_TeamId, team_id);
    teams->entries()[idx - 1]
        .ref->array_child(kTeam_Characters)
        ->push_primitive(char_id);
  }
}

void MapRoom::handle_start_game(Client &client, const Value &data) {
  auto session = state().ref_child(kRoot_Session);
  if (client.id() != session->get_string(kSession_GameOwnerId))
    return;
  if (session->get_string(kSession_Phase) != "preGame")
    return;

  bool owner_as_spectator = data.value("ownerAsSpectator", false);
  Value custom_teams = data.value("customTeams", Value::object());
  assign_teams(client.id(), owner_as_spectator, custom_teams);
  apply_spawn_positions();

  std::string clock_mode =
      game_settings_.value("gameClockMode", std::string("Off"));
  double countdown_minutes = game_settings_.value("countdownTimeMinutes", 0.0);
  bool has_countdown = clock_mode == "Count Down" && countdown_minutes > 0;

  double now = now_ms();
  auto game_session = session->ref_child(kSession_GameSession);
  session->set_number(kSession_PhaseChangedAt, now);
  game_session->set_number(kGameSession_ResultsEnd, 0);

  double countdown_end = has_countdown ? now + countdown_minutes * 60000.0 : 0;
  session->set_string(kSession_Phase, "game");
  game_session->set_string(kGameSession_Phase, "game");

  auto characters = state().map_child(kRoot_Characters);
  for (const auto &entry : characters->entries()) {
    if (entry.alive)
      grant_starting_inventory(entry.value);
  }

  apply_game_start_devices(countdown_end);
  broadcast_state_patch();
}

void MapRoom::grant_starting_inventory(
    std::shared_ptr<schema::Node> character) {
  constexpr int kInteractiveSlotCount = 5;
  std::string phase =
      state().ref_child(kRoot_Session)->get_string(kSession_Phase);
  auto inventory = character->ref_child(kChar_Inventory);
  auto slots = inventory->map_child(kInv_Slots);
  auto interactive_slots = inventory->map_child(kInv_InteractiveSlots);

  for (const Value &device : map_catalog_.devices()) {
    if (device.value("type", std::string()) != "startingInventory")
      continue;
    const Value &props = device.value("properties", Value::object());
    if (!props.value("enabled", true))
      continue;
    if (props.value("grantDuringPhase", std::string("game")) != phase)
      continue;

    std::string item_id = props.value("itemId", std::string());
    if (item_id.empty())
      continue;
    double amount = props.value("itemAmount", 1.0);

    slots->get_or_create(item_id)->set_number(kInvSlot_Amount, amount);

    for (int i = 1; i <= kInteractiveSlotCount; i++) {
      auto slot = interactive_slots->find(std::to_string(i));
      if (!slot || !slot->get_string(kSlot_ItemId).empty())
        continue;
      int clip_size = default_clip_size(item_id);
      slot->set_string(kSlot_ItemId, item_id);
      slot->set_number(kSlot_Count, amount);
      slot->set_number(kSlot_CurrentClip, clip_size);
      slot->set_number(kSlot_ClipSize, clip_size);
      if (props.value("equipOnGrant", false))
        inventory->set_number(kInv_ActiveInteractiveSlot, i);
      break;
    }
  }
}

void MapRoom::apply_spawn_positions() {
  std::unordered_map<std::string, std::vector<std::pair<double, double>>>
      pads_by_team;
  std::vector<std::pair<double, double>> any_team_pads;

  for (const Value &device : map_catalog_.devices()) {
    if (device.value("type", std::string()) != "characterSpawnPad")
      continue;
    const Value &props = device.value("properties", Value::object());
    if (props.value("phase", std::string()) != "Game")
      continue;

    double x = device.value("x", 0.0);
    double y = device.value("y", 0.0);
    std::string team_id = props.value("teamId", std::string("__ANY_TEAM__"));
    if (team_id == "__ANY_TEAM__") {
      any_team_pads.emplace_back(x, y);
    } else {
      pads_by_team[team_id].emplace_back(x, y);
    }
  }

  auto characters = state().map_child(kRoot_Characters);
  std::unordered_map<std::string, std::size_t> next_index;
  for (const auto &entry : characters->entries()) {
    if (!entry.alive)
      continue;
    std::string team_id = entry.value->get_string(kChar_TeamId);

    std::vector<std::pair<double, double>> *pads = nullptr;
    if (auto it = pads_by_team.find(team_id);
        it != pads_by_team.end() && !it->second.empty()) {
      pads = &it->second;
    } else if (!any_team_pads.empty()) {
      pads = &any_team_pads;
    }
    if (!pads)
      continue;

    std::size_t idx = next_index[team_id]++ % pads->size();
    entry.value->set_number(kChar_X, (*pads)[idx].first);
    entry.value->set_number(kChar_Y, (*pads)[idx].second);
  }
}

void MapRoom::apply_game_start_devices(double countdown_end) {
  std::string clock_mode =
      game_settings_.value("gameClockMode", std::string("Off"));
  Value changes = Value::array();

  for (const Value &device : map_catalog_.devices()) {
    std::string type = device.value("type", std::string());
    const Value &props = device.value("properties", Value::object());

    if (type == "prop" && props.contains("visibleOnGameStart")) {
      Value entry_props = Value::object();
      entry_props["GLOBAL_visible"] = props.at("visibleOnGameStart");
      entry_props["GLOBAL_healthPercent"] = 1;
      changes.push_back(Value{{"id", device.value("id", std::string())},
                              {"properties", entry_props}});
    } else if (type == "mapOptions") {
      Value entry_props = Value::object();
      entry_props["GLOBAL_gameMusicState"] = "playing";
      if (clock_mode == "Count Down") {
        entry_props["GLOBAL_countdownActive"] = true;
        entry_props["GLOBAL_countdownEndTimestamp"] = countdown_end;
        entry_props["GLOBAL_allowedToAddTimeToEndCountdown"] = true;
      } else if (clock_mode == "Count Up") {
        entry_props["GLOBAL_countupActive"] = true;
        entry_props["GLOBAL_countupStartTimestamp"] = now_ms();
      }
      changes.push_back(Value{{"id", device.value("id", std::string())},
                              {"properties", entry_props}});
    }
  }

  broadcast("DEVICES_STATES_CHANGES", DeviceStateCompiler::encode(changes));
}

} // namespace openkit::colyseus
