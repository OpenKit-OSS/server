#include "openkit/colyseus/map_room.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <utility>
#include <vector>

#include "blueboat/common/random_id.hpp"
#include "openkit/colyseus/auth_token.hpp"
#include "openkit/colyseus/device_state_compiler.hpp"
#include "openkit/game_catalog.hpp"

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

struct WeaponStats {
  int clip_size;
  double damage;
  double speed;
  double max_distance;
  double radius;
  double muzzle_forward_offset;
  double muzzle_vertical_offset;
};

const std::unordered_map<std::string, WeaponStats> &snowball_launcher_stats() {
  static const std::unordered_map<std::string, WeaponStats> table = {
      {"snowball_launcher_common", {12, 28, 6.70, 10.0, 0.225, 0.95, -0.2}},
      {"snowball_launcher_uncommon", {14, 30, 6.70, 10.0, 0.225, 0.95, -0.2}},
      {"snowball_launcher_rare", {16, 32, 6.70, 10.0, 0.225, 0.95, -0.2}},
      {"snowball_launcher_epic", {18, 34, 6.70, 10.0, 0.225, 0.95, -0.2}},
      {"snowball_launcher_legendary", {20, 36, 6.70, 10.0, 0.225, 0.95, -0.2}},
  };
  return table;
}

// TODO: support more weapons
const WeaponStats *find_weapon_stats(const std::string &item_id) {
  const auto &table = snowball_launcher_stats();
  auto it = table.find(item_id);
  return it != table.end() ? &it->second : nullptr;
}

constexpr double kPi = 3.14159265358979323846;

struct Point2 {
  double x, y;
};

Point2 rotate(double x, double y, double radians) {
  double c = std::cos(radians), s = std::sin(radians);
  return {x * c - y * s, x * s + y * c};
}

bool point_hits_prop(double px, double py, double point_radius,
                     const Value &device) {
  if (device.value("type", std::string()) != "prop")
    return false;
  const Value &props = device.value("properties", Value::object());
  if (!props.value("UseColliders", true))
    return false;

  std::string prop_id = props.value("propId", std::string());
  const Value &catalog = GameCatalog::instance().props();
  if (!catalog.contains(prop_id))
    return false;

  const Value &catalog_entry = catalog.at(prop_id);
  double catalog_scale = catalog_entry.value("scale", 1.0);
  double scale = catalog_scale * props.value("Scale", 1.0) / 100.0;
  double angle_rad = props.value("Angle", 0.0) * kPi / 180.0;
  double prop_x = device.value("x", 0.0) / 100.0;
  double prop_y = device.value("y", 0.0) / 100.0;

  const Value &image = catalog_entry.value("image", Value::object());
  double image_w = image.value("width", 0.0);
  double image_h = image.value("height", 0.0);
  double origin_offset_x =
      (catalog_entry.value("originX", 0.5) - 0.5) * image_w;
  double origin_offset_y =
      (catalog_entry.value("originY", 0.5) - 0.5) * image_h;

  const Value &colliders = catalog_entry.value("colliders", Value::object())
                               .value("topDown", Value::object());

  for (const Value &c : colliders.value("circle", Value::array())) {
    Point2 offset =
        rotate((c.value("x", 0.0) - origin_offset_x) * scale,
               (c.value("y", 0.0) - origin_offset_y) * scale, angle_rad);
    double radius = c.value("radius", 0.0) * scale;
    if (std::hypot(px - (prop_x + offset.x), py - (prop_y + offset.y)) <=
        radius + point_radius)
      return true;
  }

  for (const Value &c : colliders.value("capsule", Value::array())) {
    Point2 offset =
        rotate((c.value("x", 0.0) - origin_offset_x) * scale,
               (c.value("y", 0.0) - origin_offset_y) * scale, angle_rad);
    double cx = prop_x + offset.x, cy = prop_y + offset.y;
    double radius = c.value("radius", 0.0) * scale;
    double half_height = c.value("halfHeight", 0.0) * scale;
    double total_angle = angle_rad + c.value("angle", 0.0) * kPi / 180.0;
    Point2 local = rotate(px - cx, py - cy, -total_angle);
    double clamped_y = std::clamp(local.y, -half_height, half_height);
    if (std::hypot(local.x, local.y - clamped_y) <= radius + point_radius)
      return true;
  }

  for (const Value &c : colliders.value("rectangle", Value::array())) {
    Point2 offset =
        rotate((c.value("x", 0.0) - origin_offset_x) * scale,
               (c.value("y", 0.0) - origin_offset_y) * scale, angle_rad);
    double cx = prop_x + offset.x, cy = prop_y + offset.y;
    double half_w = c.value("width", 0.0) * scale / 2.0 + point_radius;
    double half_h = c.value("height", 0.0) * scale / 2.0 + point_radius;
    double total_angle = angle_rad + c.value("angle", 0.0) * kPi / 180.0;
    Point2 local = rotate(px - cx, py - cy, -total_angle);
    if (std::abs(local.x) <= half_w && std::abs(local.y) <= half_h)
      return true;
  }

  return false;
}

bool point_hits_device(double px, double py, double point_radius,
                       const Value &device) {
  const Value &colliders = GameCatalog::instance().device_colliders();
  std::string type = device.value("type", std::string());
  if (!colliders.contains(type))
    return false;

  const Value &spec = colliders.at(type);
  const Value &props = device.value("properties", Value::object());
  std::string requires_option = spec.value("requiresOption", std::string());
  if (!requires_option.empty() && !props.value(requires_option, false))
    return false;

  const Value &box = spec.value("box", Value::object());
  double cx = device.value("x", 0.0) / 100.0 + box.value("x", 0.0) / 100.0;
  double cy = device.value("y", 0.0) / 100.0 + box.value("y", 0.0) / 100.0;
  double width = props.value(box.value("widthOption", std::string()), 0.0);
  double height = props.value(box.value("heightOption", std::string()), 0.0) +
                  box.value("heightAdjust", 0.0);
  double half_w = width / 100.0 / 2.0 + point_radius;
  double half_h = height / 100.0 / 2.0 + point_radius;

  return std::abs(px - cx) <= half_w && std::abs(py - cy) <= half_h;
}

bool point_hits_any_prop(double px, double py, double point_radius,
                         const Value &devices) {
  for (const Value &device : devices) {
    if (point_hits_prop(px, py, point_radius, device) ||
        point_hits_device(px, py, point_radius, device))
      return true;
  }
  return false;
}

constexpr double kTerrainTileSize = 0.64;

bool point_hits_terrain(double px, double py, double point_radius,
                        const Value &terrain_tiles) {
  for (const Value &tile : terrain_tiles) {
    if (!tile.value("collides", false))
      continue;
    double x0 = tile.value("x", 0.0) * kTerrainTileSize;
    double y0 = tile.value("y", 0.0) * kTerrainTileSize;
    double closest_x = std::clamp(px, x0, x0 + kTerrainTileSize);
    double closest_y = std::clamp(py, y0, y0 + kTerrainTileSize);
    if (std::hypot(px - closest_x, py - closest_y) <= point_radius)
      return true;
  }
  return false;
}

double raycast_static_stop_distance(double start_x, double start_y,
                                    double dir_x, double dir_y,
                                    double max_distance, double point_radius,
                                    const Value &devices,
                                    const Value &terrain_tiles) {
  constexpr double kStep = 0.05;
  for (double d = kStep; d <= max_distance; d += kStep) {
    double x = start_x + dir_x * d;
    double y = start_y + dir_y * d;
    if (point_hits_any_prop(x, y, point_radius, devices) ||
        point_hits_terrain(x, y, point_radius, terrain_tiles))
      return d;
  }
  return max_distance;
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
        tick_projectiles();
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
  } else if (type == "ADD_GAME_TIME") {
    handle_add_game_time(client, data);
  } else if (type == "END_GAME") {
    handle_end_game(client, data);
  } else if (type == "KICK_PLAYER") {
    handle_kick_player(client, data);
  } else if (type == "FIRE") {
    handle_fire(client, data);
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
  countdown_end_ = countdown_end;
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
      const WeaponStats *stats = find_weapon_stats(item_id);
      int clip_size = stats ? stats->clip_size : 0;
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

namespace {
std::string find_map_options_device_id(const Value &devices) {
  for (const Value &device : devices) {
    if (device.value("type", std::string()) == "mapOptions")
      return device.value("id", std::string());
  }
  return std::string();
}
} // namespace

void MapRoom::handle_add_game_time(Client &client, const Value & /*data*/) {
  auto session = state().ref_child(kRoot_Session);
  if (client.id() != session->get_string(kSession_GameOwnerId))
    return;

  std::string map_options_id =
      find_map_options_device_id(map_catalog_.devices());
  if (map_options_id.empty())
    return;

  countdown_end_ += 60000;

  Value entry_props = Value::object();
  entry_props["GLOBAL_countdownEndTimestamp"] = countdown_end_;
  Value changes = Value::array();
  changes.push_back(Value{{"id", map_options_id}, {"properties", entry_props}});
  broadcast("DEVICES_STATES_CHANGES", DeviceStateCompiler::encode(changes));
}

void MapRoom::handle_end_game(Client &client, const Value & /*data*/) {
  auto session = state().ref_child(kRoot_Session);
  if (client.id() != session->get_string(kSession_GameOwnerId))
    return;

  double now = now_ms();
  auto game_session = session->ref_child(kSession_GameSession);
  session->set_string(kSession_Phase, "results");
  session->set_number(kSession_PhaseChangedAt, now);
  game_session->set_string(kGameSession_Phase, "results");
  broadcast_state_patch();

  Value changes = Value::array();
  std::string map_options_id =
      find_map_options_device_id(map_catalog_.devices());
  if (!map_options_id.empty()) {
    Value map_options_props = Value::object();
    map_options_props["GLOBAL_gameMusicState"] = "fadingOut";
    map_options_props["GLOBAL_countdownActive"] = false;
    changes.push_back(
        Value{{"id", map_options_id}, {"properties", map_options_props}});
  }

  auto characters = state().map_child(kRoot_Characters);
  for (const Value &device : map_catalog_.devices()) {
    if (device.value("type", std::string()) != "endOfGameWidget")
      continue;
    std::string device_id = device.value("id", std::string());
    for (const auto &entry : characters->entries()) {
      if (!entry.alive)
        continue;
      Value props = Value::object();
      props["PLAYER_" + entry.key + "_active"] = true;
      props["PLAYER_" + entry.key + "_value"] =
          entry.value->get_number(kChar_Score);
      changes.push_back(Value{{"id", device_id}, {"properties", props}});
    }
  }

  broadcast("DEVICES_STATES_CHANGES", DeviceStateCompiler::encode(changes));
}

void MapRoom::handle_kick_player(Client &client, const Value &data) {
  auto session = state().ref_child(kRoot_Session);
  if (client.id() != session->get_string(kSession_GameOwnerId))
    return;

  std::string character_id = data.value("characterId", std::string());
  if (character_id.empty() || character_id == client.id())
    return;

  Client *target = find_client_by_id(character_id);
  if (!target)
    return;

  target->send("GOT_KICKED");
  target->close();
}

void MapRoom::handle_fire(Client &client, const Value &data) {
  auto characters = state().map_child(kRoot_Characters);
  auto character = characters->find(client.id());
  if (!character)
    return;

  auto inventory = character->ref_child(kChar_Inventory);
  int active_slot =
      static_cast<int>(inventory->get_number(kInv_ActiveInteractiveSlot));
  if (active_slot <= 0)
    return;
  auto interactive_slots = inventory->map_child(kInv_InteractiveSlots);
  auto slot = interactive_slots->find(std::to_string(active_slot));
  if (!slot)
    return;

  std::string item_id = slot->get_string(kSlot_ItemId);
  const WeaponStats *stats = find_weapon_stats(item_id);
  if (!stats || slot->get_number(kSlot_CurrentClip) <= 0)
    return;

  slot->set_number(kSlot_CurrentClip, slot->get_number(kSlot_CurrentClip) - 1);
  broadcast_state_patch();

  double angle = data.value("angle", 0.0);
  double char_x = character->get_number(kChar_X) / 100.0;
  double char_y = character->get_number(kChar_Y) / 100.0;
  double start_x = char_x + std::cos(angle) * stats->muzzle_forward_offset;
  double start_y = char_y + std::sin(angle) * stats->muzzle_forward_offset +
                   stats->muzzle_vertical_offset;
  double end_x = start_x + std::cos(angle) * stats->max_distance;
  double end_y = start_y + std::sin(angle) * stats->max_distance;

  double now = now_ms();
  double travel_seconds = stats->max_distance / stats->speed;
  double end_time = now + travel_seconds * 1000.0;
  double fragility = game_settings_.value("startingFragility", 0.0);
  std::string owner_team_id = character->get_string(kChar_TeamId);
  std::string projectile_id = blueboat::random_id(9);

  double dir_x = std::cos(angle), dir_y = std::sin(angle);
  double static_stop = raycast_static_stop_distance(
      start_x, start_y, dir_x, dir_y, stats->max_distance, 0.0,
      map_catalog_.devices(), map_catalog_.terrain());
  double hit_x = start_x + dir_x * static_stop;
  double hit_y = start_y + dir_y * static_stop;
  double hit_time = now + (static_stop / stats->speed) * 1000.0;

  Value projectile{
      {"id", projectile_id},
      {"startTime", now},
      {"endTime", end_time},
      {"start", Value{{"x", start_x}, {"y", start_y}}},
      {"end", Value{{"x", end_x}, {"y", end_y}}},
      {"radius", stats->radius},
      {"hitTime", hit_time},
      {"hitPos", Value{{"x", hit_x}, {"y", hit_y}}},
      {"appearance", "snowball"},
      {"ownerId", client.id()},
      {"ownerTeamId", owner_team_id},
      {"canDamagePlayersWhenPvpDisabled", false},
      {"damage", stats->damage},
      {"hitTimeFragility", fragility},
  };
  Value added = Value::array();
  added.push_back(projectile);
  broadcast("PROJECTILE_CHANGES",
            Value{{"added", added}, {"hit", Value::array()}});

  in_flight_projectiles_.push_back(InFlightProjectile{
      projectile_id, client.id(), owner_team_id, start_x, start_y, dir_x, dir_y,
      stats->speed, static_stop, stats->damage, fragility, now});
}

void MapRoom::tick_projectiles() {
  if (in_flight_projectiles_.empty())
    return;

  // TODO: more accurate player hitbox
  constexpr double kCharacterHitRadius = 0.183;
  bool pvp_enabled = game_settings_.value("playerVsPlayerDamageEnabled", true);
  auto characters = state().map_child(kRoot_Characters);
  double now = now_ms();

  std::vector<InFlightProjectile> still_flying;
  for (auto &p : in_flight_projectiles_) {
    double elapsed_s = (now - p.start_time_ms) / 1000.0;
    double traveled = std::min(p.speed * elapsed_s, p.max_distance);
    double cur_x = p.start_x + p.dir_x * traveled;
    double cur_y = p.start_y + p.dir_y * traveled;

    std::shared_ptr<schema::Node> hit_character;
    std::string hit_character_id;
    if (pvp_enabled) {
      for (const auto &entry : characters->entries()) {
        if (!entry.alive || entry.key == p.owner_id)
          continue;
        std::string target_team = entry.value->get_string(kChar_TeamId);
        if (p.owner_team_id != "__NO_TEAM_ID" && target_team == p.owner_team_id)
          continue;
        double px = entry.value->get_number(kChar_X) / 100.0;
        double py = entry.value->get_number(kChar_Y) / 100.0;
        if (std::hypot(px - cur_x, py - cur_y) <= kCharacterHitRadius) {
          hit_character = entry.value;
          hit_character_id = entry.key;
          break;
        }
      }
    }

    bool reached_end = traveled >= p.max_distance;
    if (!hit_character && !reached_end) {
      still_flying.push_back(p);
      continue;
    }
    if (!hit_character)
      continue;

    auto health = hit_character->ref_child(kChar_Health);
    double shield = health->get_number(kHealth_Shield);
    std::string hit_type;
    if (shield > 0) {
      health->set_number(kHealth_Shield, std::max(0.0, shield - p.damage));
      hit_type = "s";
    } else {
      double hp = health->get_number(kHealth_Health);
      health->set_number(kHealth_Health, std::max(0.0, hp - p.damage));
      hit_type = "h";
    }

    Value hits = Value::array();
    hits.push_back(Value{{"characterId", hit_character_id},
                         {"damage", p.damage},
                         {"type", hit_type},
                         {"hitTimeFragility", p.fragility}});
    Value hit_list = Value::array();
    hit_list.push_back(
        Value{{"id", p.id}, {"x", cur_x}, {"y", cur_y}, {"hits", hits}});
    broadcast("PROJECTILE_CHANGES",
              Value{{"added", Value::array()}, {"hit", hit_list}});
  }
  in_flight_projectiles_ = std::move(still_flying);
}

} // namespace openkit::colyseus
