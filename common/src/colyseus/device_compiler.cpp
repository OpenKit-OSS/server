#include "openkit/colyseus/device_compiler.hpp"

#include <unordered_map>

namespace openkit::colyseus {

namespace {

Value resolve(const Value &values, const Value &index_or_null) {
  if (!index_or_null.is_number_integer()) return Value();
  std::size_t idx = index_or_null.get<std::size_t>();
  if (idx >= values.size()) return Value();
  return values[idx];
}

}  // namespace

Value DeviceCompiler::decode(const Value &added_devices) {
  Value out = Value::array();
  const Value &values = added_devices.value("values", Value::array());
  const Value &devices = added_devices.value("devices", Value::array());

  for (const Value &d : devices) {
    if (!d.is_array() || d.size() < 7) continue;

    Value properties = Value::object();
    for (const Value &pair : d[6]) {
      if (!pair.is_array() || pair.size() != 2) continue;
      Value key = resolve(values, pair[0]);
      if (!key.is_string()) continue;
      properties[key.get<std::string>()] = resolve(values, pair[1]);
    }

    out.push_back(Value{
        {"id", d[0]},
        {"x", d[1]},
        {"y", d[2]},
        {"z", d[3]},
        {"category", resolve(values, d[4])},
        {"type", resolve(values, d[5])},
        {"properties", properties},
    });
  }

  return out;
}

namespace {

// Interns a Value into the pool, deduplicating by its canonical JSON text -
// structurally-equal values (same type and content) always dump() the same
// way, which is all we need for a stable, collision-free dedup key.
class ValuePool {
public:
  int intern(const Value &v) {
    std::string key = v.dump();
    auto it = index_.find(key);
    if (it != index_.end()) return it->second;
    int idx = static_cast<int>(values_.size());
    values_.push_back(v);
    index_.emplace(std::move(key), idx);
    return idx;
  }

  Value take_values() { return Value(std::move(values_)); }

private:
  std::vector<Value> values_;
  std::unordered_map<std::string, int> index_;
};

}  // namespace

Value DeviceCompiler::encode(const Value &devices) {
  ValuePool pool;
  Value out_devices = Value::array();

  for (const Value &d : devices) {
    Value category = d.value("category", Value());
    Value type = d.value("type", Value());

    Value pairs = Value::array();
    const Value &properties = d.value("properties", Value::object());
    for (auto it = properties.begin(); it != properties.end(); ++it) {
      pairs.push_back(Value::array({pool.intern(Value(it.key())), pool.intern(it.value())}));
    }

    Value id = d.value("id", std::string());
    Value x = d.value("x", 0.0);
    Value y = d.value("y", 0.0);
    Value z = d.contains("z") ? d.at("z") : Value();
    Value category_idx = category.is_null() ? Value() : Value(pool.intern(category));
    Value type_idx = type.is_null() ? Value() : Value(pool.intern(type));

    Value entry = Value::array();
    entry.push_back(id);
    entry.push_back(x);
    entry.push_back(y);
    entry.push_back(z);
    entry.push_back(category_idx);
    entry.push_back(type_idx);
    entry.push_back(pairs);
    out_devices.push_back(entry);
  }

  return Value{
      {"values", pool.take_values()},
      {"devices", out_devices},
  };
}

}  // namespace openkit::colyseus
