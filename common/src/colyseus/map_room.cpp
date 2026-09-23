#include "openkit/colyseus/map_room.hpp"

#include <chrono>

#include "openkit/colyseus/auth_token.hpp"

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
enum AppearanceField { kAppearance_Skin = 0 };
enum MatchmakerField { kMatchmaker_GameCode = 0 };
enum HooksField { kHooks_HookJSON = 0 };

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

void MapRoom::on_dispose() {
  tick_timer_.clear();
  phase_timer_.clear();
}

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
  character->ref_child(kChar_ClassDesigner);
  character->ref_child(kChar_Appearance)
      ->set_string(kAppearance_Skin, R"({"id":"character_truffle"})");
  auto permissions = character->ref_child(kChar_Permissions);
  permissions->set_bool(kPerms_Adding, false);
  permissions->set_bool(kPerms_Removing, false);
  permissions->set_bool(kPerms_Editing, false);
  permissions->set_bool(kPerms_ManageCodeGrids, false);
  auto inventory = character->ref_child(kChar_Inventory);
  inventory->map_child(kInv_Slots);
  inventory->set_number(kInv_MaxSlots, 999);
  inventory->set_number(kInv_ActiveInteractiveSlot, 0);
  auto interactive_slots = inventory->map_child(kInv_InteractiveSlots);
  for (int i = 1; i <= 4; i++) {
    auto slot = interactive_slots->get_or_create(std::to_string(i));
    slot->set_string(kSlot_ItemId, "");
    slot->set_bool(kSlot_Waiting, false);
    slot->set_number(kSlot_WaitingStartTime, 0);
    slot->set_number(kSlot_WaitingEndTime, 0);
    slot->set_number(kSlot_CurrentClip, 0);
    slot->set_number(kSlot_ClipSize, 0);
    slot->set_number(kSlot_Durability, -1);
    slot->set_number(kSlot_Count, 0);
  }
  inventory->array_child(kInv_InteractiveSlotsOrder);
  inventory->set_bool(kInv_InfiniteAmmo, false);
  character->ref_child(kChar_Xp);
  character->ref_child(kChar_Assignment);
  auto health = character->ref_child(kChar_Health);
  health->set_number(kHealth_Fragility, 0);
  health->set_number(kHealth_Health, 100);
  health->set_number(kHealth_Shield, 100);
  health->set_number(kHealth_MaxHealth, 100);
  health->set_number(kHealth_MaxShield, 100);
  health->set_number(kHealth_Lives, 3);
  health->set_bool(kHealth_SpawnImmunityActive, false);
  health->set_bool(kHealth_ClassImmunityActive, false);
  health->set_bool(kHealth_ShowHealthBar, true);
  character->ref_child(kChar_Projectiles);
  auto zone = character->ref_child(kChar_ZoneAbilitiesOverrides);
  zone->set_bool(kZone_AllowWeaponFire, true);
  zone->set_bool(kZone_AllowWeaponDrop, true);
  zone->set_bool(kZone_AllowItemDrop, true);
  zone->set_bool(kZone_AllowResourceDrop, true);
  auto physics = character->ref_child(kChar_Physics);
  physics->set_bool(kPhysics_IsGrounded, false);
  physics->set_bool(kPhysics_IsWallSliding, false);

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

void MapRoom::assign_teams(const std::string &owner_id,
                           bool owner_as_spectator,
                           const Value &custom_teams) {
  std::string team_mode = game_settings_.value("teams", std::string("Free For All"));
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
    if (custom_teams.contains(char_id) && custom_teams.at(char_id).is_string()) {
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
    teams->entries()[idx - 1].ref->array_child(kTeam_Characters)->push_primitive(char_id);
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

  std::string clock_mode = game_settings_.value("gameClockMode", std::string("Off"));
  double countdown_minutes = game_settings_.value("countdownTimeMinutes", 0.0);
  bool has_countdown = clock_mode == "Count Down" && countdown_minutes > 0;

  double now = now_ms();
  auto game_session = session->ref_child(kSession_GameSession);
  session->set_number(kSession_PhaseChangedAt, now);
  game_session->set_number(kGameSession_ResultsEnd, 0);

  if (has_countdown) {
    double countdown_end = now + countdown_minutes * 60000.0;
    session->set_string(kSession_Phase, "countdown");
    game_session->set_string(kGameSession_Phase, "countdown");
    game_session->set_number(kGameSession_CountdownEnd, countdown_end);
    schedule_phase_change("live", countdown_end - now);
  } else {
    session->set_string(kSession_Phase, "live");
    game_session->set_string(kGameSession_Phase, "live");
    game_session->set_number(kGameSession_CountdownEnd, 0);
  }

  broadcast_state_patch();
}

void MapRoom::schedule_phase_change(const std::string &new_phase, double delay_ms) {
  if (delay_ms < 0)
    delay_ms = 0;
  phase_timer_ = blueboat::Scheduler::instance().set_timeout(
      std::chrono::milliseconds(static_cast<long long>(delay_ms)),
      [this, new_phase] {
        std::lock_guard<std::recursive_mutex> guard(mutex());
        auto session = state().ref_child(kRoot_Session);
        session->set_string(kSession_Phase, new_phase);
        session->set_number(kSession_PhaseChangedAt, now_ms());
        session->ref_child(kSession_GameSession)
            ->set_string(kGameSession_Phase, new_phase);
        broadcast_state_patch();
      });
}

} // namespace openkit::colyseus
