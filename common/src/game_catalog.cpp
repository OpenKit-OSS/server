#include "openkit/game_catalog.hpp"

#include <stdexcept>

#include "openkit/data_archive.hpp"

namespace openkit {

namespace {

Value load_catalog(const std::string &file) {
  std::string path = "catalog/" + file;
  auto entry = data_archive().find(path);
  if (!entry) {
    throw std::runtime_error("openkit: no bundled catalog: " + file);
  }
  return Value::parse(data_archive().load_text(*entry));
}

} // namespace

GameCatalog::GameCatalog()
    : props_(load_catalog("props.json")), items_(load_catalog("items.json")),
      devices_(load_catalog("devices.json")),
      terrain_(load_catalog("terrain.json")) {}

const GameCatalog &GameCatalog::instance() {
  static const GameCatalog catalog;
  return catalog;
}

} // namespace openkit
