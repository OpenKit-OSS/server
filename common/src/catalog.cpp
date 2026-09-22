#include "openkit/catalog.hpp"

#include <stdexcept>

#include "openkit/data_archive.hpp"

namespace openkit {

Catalog Catalog::load(const std::string &gamemode_name) {
  std::string path = gamemode_name + "/static_state.json";
  auto entry = data_archive().find(path);
  if (!entry) {
    throw std::runtime_error(
        "openkit: no bundled static_state.json for gamemode: " + gamemode_name);
  }
  Value data = Value::parse(data_archive().load_text(*entry));
  return Catalog(std::move(data));
}

std::optional<Value> Catalog::find_by_name(const Value &array,
                                           const std::string &name) const {
  for (const auto &item : array) {
    if (item.value("name", std::string()) == name) {
      return item;
    }
  }
  return std::nullopt;
}

} // namespace openkit
