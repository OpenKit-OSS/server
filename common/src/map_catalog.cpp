#include "openkit/map_catalog.hpp"

#include <stdexcept>

#include "openkit/colyseus/device_compiler.hpp"
#include "openkit/colyseus/terrain_compiler.hpp"
#include "openkit/data_archive.hpp"
#include "openkit/game_catalog.hpp"

namespace openkit {

namespace {

Value load_json(const std::string &map_id, const std::string &file) {
  std::string path = "maps/" + map_id + "/" + file;
  auto entry = data_archive().find(path);
  if (!entry) {
    throw std::runtime_error("openkit: no bundled " + file +
                             " for map: " + map_id);
  }
  return Value::parse(data_archive().load_text(*entry));
}

double number_or(const Value &v, const std::string &key, double fallback) {
  if (!v.contains(key) || v.at(key).is_null())
    return fallback;
  return v.at(key).get<double>();
}

Value device_from_bundle(const Value &d) {
  return Value{
      {"id", d.value("id", std::string())},
      {"x", number_or(d, "x", 0.0)},
      {"y", number_or(d, "y", 0.0)},
      {"z", number_or(d, "depth", 0.0)},
      {"category", d.value("layerId", Value())},
      {"type", d.value("deviceId", Value())},
      {"properties", d.value("options", Value::object())},
  };
}

Value build_props_options(const Value &devices) {
  const GameCatalog &catalog = GameCatalog::instance();
  Value added = Value::array();
  Value seen = Value::object();
  for (const Value &d : devices) {
    if (d.value("type", std::string()) != "prop")
      continue;
    std::string prop_id =
        d.value("properties", Value::object()).value("propId", std::string());
    if (prop_id.empty() || seen.contains(prop_id))
      continue;
    seen[prop_id] = true;
    if (catalog.props().contains(prop_id))
      added.push_back(catalog.props().at(prop_id));
  }
  return Value{{"addedPropsOptions", added}, {"initial", true}};
}

Value build_world_options(const std::string &map_style) {
  const GameCatalog &catalog = GameCatalog::instance();

  Value terrain_options = Value::array();
  for (auto it = catalog.terrain().begin(); it != catalog.terrain().end();
       ++it) {
    const Value &blocked = it.value().value("blockedMapStyles", Value::array());
    bool is_blocked = false;
    for (const Value &s : blocked) {
      if (s.get<std::string>() == map_style) {
        is_blocked = true;
        break;
      }
    }
    if (!is_blocked)
      terrain_options.push_back(it.value());
  }

  Value item_options = Value::array();
  for (auto it = catalog.items().begin(); it != catalog.items().end(); ++it) {
    item_options.push_back(it.value());
  }

  Value device_options = Value::array();
  for (auto it = catalog.devices().begin(); it != catalog.devices().end();
       ++it) {
    device_options.push_back(Value{
        {"id", it.key()},
        {"defaultState",
         it.value().value("defaultState", Value::object()).dump()},
    });
  }

  Value code_grids = Value{{"blockCategories", "[]"}, {"customBlocks", "[]"}};

  return Value{
      {"terrainOptions", terrain_options},
      {"itemOptions", item_options},
      {"deviceOptions", device_options},
      {"codeGrids", code_grids},
  };
}

} // namespace

MapCatalog MapCatalog::load(const std::string &map_id) {
  Value bundle_map = load_json(map_id, "bundle_map.json");

  Value devices = Value::array();
  for (const Value &d : bundle_map.value("devices", Value::array())) {
    devices.push_back(device_from_bundle(d));
  }

  Value map_settings = Value::object();
  for (const Value &d : devices) {
    if (d.value("type", std::string()) == "mapOptions") {
      map_settings = d.value("properties", Value::object());
      break;
    }
  }

  Value world_changes = Value{
      {"devices",
       Value{{"addedDevices", colyseus::DeviceCompiler::encode(devices)},
             {"removedDevices", Value::array()},
             {"initial", true}}},
      {"propsOptions", build_props_options(devices)},
  };

  Value terrain_changes = colyseus::TerrainCompiler::encode(
      bundle_map.value("terrain", Value::array()));

  Value world_options = build_world_options("topDown");

  Value world = Value{{"width", 250}, {"height", 250}};

  return MapCatalog(map_id, world_options, world_changes, terrain_changes,
                    load_json(map_id, "devices_states_changes.json"),
                    map_settings, world, devices,
                    bundle_map.value("terrain", Value::array()));
}

} // namespace openkit
