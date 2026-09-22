#pragma once

#include <optional>
#include <string>

#include "blueboat/common/json.hpp"

namespace openkit {

using blueboat::Value;

class Catalog {
public:
  static Catalog load(const std::string &gamemode_name);

  const Value &game_options_defaults() const { return data_.at("gameOptions"); }
  const Value &powerups() const { return data_.at("powerups"); }
  const Value &upgrades() const { return data_.at("upgrades"); }
  const Value &themes() const { return data_.at("themes"); }
  const Value &disabled_themes() const { return data_.at("disabledThemes"); }
  const Value &news() const { return data_.at("news"); }

  std::optional<Value> find_by_name(const Value &array,
                                    const std::string &name) const;

private:
  explicit Catalog(Value data) : data_(std::move(data)) {}

  Value data_;
};

} // namespace openkit
