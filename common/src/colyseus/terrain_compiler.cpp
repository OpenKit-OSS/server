#include "openkit/colyseus/terrain_compiler.hpp"

#include <unordered_map>

namespace openkit::colyseus {

namespace {

class ValuePool {
public:
  int intern(const std::string &v) {
    auto it = index_.find(v);
    if (it != index_.end())
      return it->second;
    int idx = static_cast<int>(values_.size());
    values_.push_back(v);
    index_.emplace(v, idx);
    return idx;
  }

  Value take_values() { return Value(std::move(values_)); }

private:
  std::vector<std::string> values_;
  std::unordered_map<std::string, int> index_;
};

double number_or(const Value &v, const std::string &key, double fallback) {
  if (!v.contains(key) || v.at(key).is_null())
    return fallback;
  return v.at(key).get<double>();
}

} // namespace

Value TerrainCompiler::decode(const Value &terrain_changes) {
  Value out = Value::array();
  const Value &added = terrain_changes.value("added", Value::object());
  const Value &terrains = added.value("terrains", Value::array());
  const Value &tiles = added.value("tiles", Value::array());

  for (const Value &t : tiles) {
    if (!t.is_array() || t.size() != 7)
      continue;
    double x = t[0];
    double y = t[1];
    std::size_t terrain_idx = t[2].get<std::size_t>();
    double depth = t[4];
    std::string terrain = terrain_idx < terrains.size()
                              ? terrains[terrain_idx].get<std::string>()
                              : std::string();

    out.push_back(Value{{"x", x},
                        {"y", y},
                        {"terrain", terrain},
                        {"depth", depth},
                        {"variant", Value::array({t[5], t[6]})}});
  }

  return out;
}

Value TerrainCompiler::encode(const Value &tiles, int update_id, bool initial) {
  ValuePool pool;
  Value out_tiles = Value::array();

  for (const Value &tile : tiles) {
    std::string terrain = tile.value("terrain", std::string());
    int idx = pool.intern(terrain);
    double depth = tile.value("depth", 0.0);
    const Value &variant = tile.value("variant", Value::array());
    Value variant_a = variant.size() > 0 ? variant[0] : Value(0);
    Value variant_b = variant.size() > 1 ? variant[1] : Value(0);

    Value entry = Value::array();
    entry.push_back(tile.value("x", 0.0));
    entry.push_back(tile.value("y", 0.0));
    entry.push_back(idx);
    entry.push_back(idx);
    entry.push_back(depth);
    entry.push_back(variant_a);
    entry.push_back(variant_b);
    out_tiles.push_back(entry);
  }

  return Value{
      {"added", Value{{"terrains", pool.take_values()}, {"tiles", out_tiles}}},
      {"removedTiles", Value::array()},
      {"initial", initial},
      {"updateId", update_id},
  };
}

} // namespace openkit::colyseus
