#include "openkit/colyseus/device_state_compiler.hpp"

#include <unordered_map>

namespace openkit::colyseus {

namespace {

Value resolve(const Value &values, const Value &index_or_null) {
  if (!index_or_null.is_number_integer())
    return Value();
  std::size_t idx = index_or_null.get<std::size_t>();
  if (idx >= values.size())
    return Value();
  return values[idx];
}

} // namespace

Value DeviceStateCompiler::decode(const Value &message) {
  Value out = Value::array();
  const Value &values = message.value("values", Value::array());
  const Value &changes = message.value("changes", Value::array());

  for (const Value &change : changes) {
    if (!change.is_array() || change.size() != 3)
      continue;
    const Value &id = change[0];
    const Value &keys = change[1];
    const Value &vals = change[2];

    Value properties = Value::object();
    for (std::size_t i = 0; i < keys.size() && i < vals.size(); i++) {
      Value key = resolve(values, keys[i]);
      if (!key.is_string())
        continue;
      properties[key.get<std::string>()] = vals[i];
    }

    out.push_back(Value{{"id", id}, {"properties", properties}});
  }

  return out;
}

namespace {

class KeyPool {
public:
  int intern(const std::string &key) {
    auto it = index_.find(key);
    if (it != index_.end())
      return it->second;
    int idx = static_cast<int>(keys_.size());
    keys_.push_back(key);
    index_.emplace(key, idx);
    return idx;
  }

  Value take_keys() { return Value(std::move(keys_)); }

private:
  std::vector<std::string> keys_;
  std::unordered_map<std::string, int> index_;
};

} // namespace

Value DeviceStateCompiler::encode(const Value &changes,
                                  const Value &removed_ids, bool initial) {
  KeyPool pool;
  Value out_changes = Value::array();

  for (const Value &change : changes) {
    Value id = change.value("id", std::string());
    const Value &properties = change.value("properties", Value::object());

    Value keys = Value::array();
    Value vals = Value::array();
    for (auto it = properties.begin(); it != properties.end(); ++it) {
      keys.push_back(pool.intern(it.key()));
      vals.push_back(it.value());
    }

    Value entry = Value::array();
    entry.push_back(id);
    entry.push_back(keys);
    entry.push_back(vals);
    out_changes.push_back(entry);
  }

  return Value{{"values", pool.take_keys()},
              {"changes", out_changes},
              {"removedIds", removed_ids},
              {"initial", initial}};
}

} // namespace openkit::colyseus
