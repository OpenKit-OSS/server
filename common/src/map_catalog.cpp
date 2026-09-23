#include "openkit/map_catalog.hpp"

#include <stdexcept>

#include "openkit/data_archive.hpp"

namespace openkit {

namespace {

Value load_json(const std::string &map_id, const std::string &file) {
  std::string path = "maps/" + map_id + "/" + file;
  auto entry = data_archive().find(path);
  if (!entry) {
    throw std::runtime_error("openkit: no bundled " + file + " for map: " + map_id);
  }
  return Value::parse(data_archive().load_text(*entry));
}

}  // namespace

MapCatalog MapCatalog::load(const std::string &map_id) {
  return MapCatalog(
      map_id,
      load_json(map_id, "world_options.json"),
      load_json(map_id, "world_changes.json"),
      load_json(map_id, "terrain_changes.json"),
      load_json(map_id, "devices_states_changes.json"),
      load_json(map_id, "map_settings.json"),
      load_json(map_id, "world.json"));
}

}  // namespace openkit
