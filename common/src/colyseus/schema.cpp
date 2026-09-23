#include "openkit/colyseus/schema.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace openkit::colyseus::schema {

namespace {

constexpr std::uint8_t kSwitchToStructure = 255;
constexpr std::uint8_t kOpAdd = 128;
constexpr std::uint8_t kOpReplace = 0;
constexpr std::uint8_t kOpDelete = 64;

void encode_uint8(std::string &out, std::uint8_t v) {
  out.push_back(static_cast<char>(v));
}

void encode_uint16(std::string &out, std::uint16_t v) {
  out.push_back(static_cast<char>(v & 0xFF));
  out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void encode_uint32(std::string &out, std::uint32_t v) {
  out.push_back(static_cast<char>(v & 0xFF));
  out.push_back(static_cast<char>((v >> 8) & 0xFF));
  out.push_back(static_cast<char>((v >> 16) & 0xFF));
  out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

void encode_float64(std::string &out, double v) {
  std::uint64_t bits;
  static_assert(sizeof(bits) == sizeof(v), "double must be 8 bytes");
  std::memcpy(&bits, &v, sizeof(bits));
  encode_uint32(out, static_cast<std::uint32_t>(bits & 0xFFFFFFFFu));
  encode_uint32(out, static_cast<std::uint32_t>(bits >> 32));
}

void encode_number(std::string &out, double value) {
  if (std::isnan(value)) {
    encode_number(out, 0);
    return;
  }
  if (!std::isfinite(value)) {
    encode_number(out, value > 0 ? 9007199254740991.0 : -9007199254740991.0);
    return;
  }

  bool is_integer = (value == std::floor(value)) && std::fabs(value) < 1.0e15;
  if (!is_integer) {
    encode_uint8(out, 0xcb);
    encode_float64(out, value);
    return;
  }

  if (value >= 0) {
    if (value < 0x80) {
      encode_uint8(out, static_cast<std::uint8_t>(value));
    } else if (value < 0x100) {
      encode_uint8(out, 0xcc);
      encode_uint8(out, static_cast<std::uint8_t>(value));
    } else if (value < 0x10000) {
      encode_uint8(out, 0xcd);
      encode_uint16(out, static_cast<std::uint16_t>(value));
    } else if (value < 4294967296.0) {
      encode_uint8(out, 0xce);
      encode_uint32(out, static_cast<std::uint32_t>(value));
    } else {
      encode_uint8(out, 0xcb);
      encode_float64(out, value);
    }
  } else {
    if (value >= -0x20) {
      encode_uint8(out, static_cast<std::uint8_t>(
                            0xe0 | (static_cast<int>(value) + 0x20)));
    } else if (value >= -0x80) {
      encode_uint8(out, 0xd0);
      encode_uint8(out, static_cast<std::uint8_t>(static_cast<int>(value)));
    } else if (value >= -0x8000) {
      encode_uint8(out, 0xd1);
      encode_uint16(out, static_cast<std::uint16_t>(static_cast<int>(value)));
    } else if (value >= -2147483648.0) {
      encode_uint8(out, 0xd2);
      encode_uint32(out,
                    static_cast<std::uint32_t>(static_cast<int32_t>(value)));
    } else {
      encode_uint8(out, 0xcb);
      encode_float64(out, value);
    }
  }
}

void encode_string(std::string &out, const std::string &value) {
  std::size_t length = value.size();
  if (length < 0x20) {
    encode_uint8(out, static_cast<std::uint8_t>(0xa0 | length));
  } else if (length < 0x100) {
    encode_uint8(out, 0xd9);
    encode_uint8(out, static_cast<std::uint8_t>(length));
  } else if (length < 0x10000) {
    encode_uint8(out, 0xda);
    encode_uint16(out, static_cast<std::uint16_t>(length));
  } else {
    encode_uint8(out, 0xdb);
    encode_uint32(out, static_cast<std::uint32_t>(length));
  }
  out += value;
}

void encode_bool(std::string &out, bool value) {
  encode_uint8(out, value ? 1 : 0);
}

void encode_primitive(std::string &out, Wire wire, const Value &value) {
  switch (wire) {
  case Wire::String:
    encode_string(out,
                  value.is_string() ? value.get<std::string>() : std::string());
    break;
  case Wire::Number:
    encode_number(out, value.is_number() ? value.get<double>() : 0.0);
    break;
  case Wire::Boolean:
    encode_bool(out, !value.is_null() && value.get<bool>());
    break;
  default:
    break;
  }
}

} // namespace

#include "schema_classes.inc"

Node::Node(int class_id, const std::vector<ClassDef> *table)
    : class_id_(class_id), table_(table) {
  std::size_t n = def().fields.size();
  primitives_.resize(n);
  refs_.resize(n);
  maps_.resize(n);
  arrays_.resize(n);
}

void Node::set_string(int field, const std::string &value) {
  Value v(value);
  if (primitives_[field] != v) {
    primitives_[field] = v;
    touch(field);
  }
}

void Node::set_number(int field, double value) {
  Value v(value);
  if (!primitives_[field].is_number() ||
      primitives_[field].get<double>() != value) {
    primitives_[field] = v;
    touch(field);
  }
}

void Node::set_bool(int field, bool value) {
  Value v(value);
  if (!primitives_[field].is_boolean() ||
      primitives_[field].get<bool>() != value) {
    primitives_[field] = v;
    touch(field);
  }
}

const std::string &Node::get_string(int field) const {
  static const std::string empty;
  return primitives_[field].is_string()
             ? primitives_[field].get_ref<const std::string &>()
             : empty;
}

double Node::get_number(int field) const {
  return primitives_[field].is_number() ? primitives_[field].get<double>()
                                        : 0.0;
}

bool Node::get_bool(int field) const {
  return primitives_[field].is_boolean() && primitives_[field].get<bool>();
}

std::shared_ptr<Node> Node::ref_child(int field) {
  if (!refs_[field]) {
    refs_[field] =
        std::make_shared<Node>(def().fields[field].ref_class, table_);
    touch(field);
  }
  return refs_[field];
}

std::shared_ptr<MapNode> Node::map_child(int field) {
  if (!maps_[field]) {
    maps_[field] =
        std::make_shared<MapNode>(def().fields[field].ref_class, table_);
    touch(field);
  }
  return maps_[field];
}

std::shared_ptr<ArrayNode> Node::array_child(int field) {
  if (!arrays_[field]) {
    const FieldDef &fd = def().fields[field];
    Wire prim_wire = (fd.wire == Wire::ArrayOfNumber)   ? Wire::ArrayOfNumber
                     : (fd.wire == Wire::ArrayOfString) ? Wire::ArrayOfString
                                                        : Wire::ArrayOfRef;
    arrays_[field] =
        std::make_shared<ArrayNode>(fd.ref_class, prim_wire, table_);
    touch(field);
  }
  return arrays_[field];
}

std::shared_ptr<Node> MapNode::get_or_create(const std::string &key) {
  auto it = index_by_key_.find(key);
  if (it != index_by_key_.end() && entries_[it->second].alive) {
    return entries_[it->second].value;
  }
  auto node = std::make_shared<Node>(child_class_, table_);
  int idx = static_cast<int>(entries_.size());
  entries_.push_back(Entry{key, node, true});
  index_by_key_[key] = idx;
  dirty_added_.insert(idx);
  return node;
}

std::shared_ptr<Node> MapNode::find(const std::string &key) const {
  auto it = index_by_key_.find(key);
  if (it == index_by_key_.end() || !entries_[it->second].alive)
    return nullptr;
  return entries_[it->second].value;
}

void MapNode::erase(const std::string &key) {
  auto it = index_by_key_.find(key);
  if (it == index_by_key_.end())
    return;
  entries_[it->second].alive = false;
  dirty_added_.erase(it->second);
  dirty_removed_.insert(it->second);
  index_by_key_.erase(it);
}

std::shared_ptr<Node> ArrayNode::push_ref() {
  auto node = std::make_shared<Node>(child_class_, table_);
  int idx = static_cast<int>(values_.size());
  values_.push_back(Entry{node, Value()});
  dirty_added_.insert(idx);
  return node;
}

void ArrayNode::push_primitive(const Value &v) {
  int idx = static_cast<int>(values_.size());
  values_.push_back(Entry{nullptr, v});
  dirty_added_.insert(idx);
}

namespace {

int ensure_node_ref_id(Node &n, int &next_ref_id) {
  if (!n.has_ref_id())
    n.set_ref_id(next_ref_id++);
  return n.ref_id();
}
int ensure_map_ref_id(MapNode &m, int &next_ref_id) {
  if (!m.has_ref_id())
    m.set_ref_id(next_ref_id++);
  return m.ref_id();
}
int ensure_array_ref_id(ArrayNode &a, int &next_ref_id) {
  if (!a.has_ref_id())
    a.set_ref_id(next_ref_id++);
  return a.ref_id();
}

void visit_node(Node &node, std::string &out, bool is_root, bool all_mode,
                int &next_ref_id);
void visit_map(MapNode &map, std::string &out, bool all_mode, int &next_ref_id);
void visit_array(ArrayNode &arr, std::string &out, bool all_mode,
                 int &next_ref_id);

void encode_field_value(Node &node, int field, std::string &out, bool all_mode,
                        int &next_ref_id) {
  const FieldDef &fd = node.def().fields[field];
  switch (fd.wire) {
  case Wire::String:
  case Wire::Number:
  case Wire::Boolean: {
    Value v;
    if (fd.wire == Wire::String)
      v = node.get_string(field);
    else if (fd.wire == Wire::Number)
      v = node.get_number(field);
    else
      v = node.get_bool(field);
    encode_primitive(out, fd.wire, v);
    break;
  }
  case Wire::Ref: {
    auto child = node.ref_child(field);
    encode_number(out, ensure_node_ref_id(*child, next_ref_id));
    break;
  }
  case Wire::MapOfRef: {
    auto child = node.map_child(field);
    encode_number(out, ensure_map_ref_id(*child, next_ref_id));
    break;
  }
  case Wire::ArrayOfRef:
  case Wire::ArrayOfNumber:
  case Wire::ArrayOfString: {
    auto child = node.array_child(field);
    encode_number(out, ensure_array_ref_id(*child, next_ref_id));
    break;
  }
  }
}

void visit_node(Node &node, std::string &out, bool is_root, bool all_mode,
                int &next_ref_id) {
  if (!is_root) {
    encode_uint8(out, kSwitchToStructure);
    encode_number(out, ensure_node_ref_id(node, next_ref_id));
  }

  if (all_mode) {
    for (int field : node.all_touched()) {
      encode_uint8(out, static_cast<std::uint8_t>(field | kOpAdd));
      encode_field_value(node, field, out, all_mode, next_ref_id);
    }
  } else {
    for (int field : node.dirty()) {
      const FieldDef &fd = node.def().fields[field];
      bool is_ref_like =
          (fd.wire == Wire::Ref || fd.wire == Wire::MapOfRef ||
           fd.wire == Wire::ArrayOfRef || fd.wire == Wire::ArrayOfNumber ||
           fd.wire == Wire::ArrayOfString);
      std::uint8_t op = is_ref_like ? kOpAdd : kOpReplace;
      encode_uint8(out, static_cast<std::uint8_t>(field | op));
      encode_field_value(node, field, out, all_mode, next_ref_id);
    }
    node.clear_dirty();
  }

  for (int field = 0; field < static_cast<int>(node.def().fields.size());
       field++) {
    const FieldDef &fd = node.def().fields[field];
    if (fd.wire == Wire::Ref) {
      if (auto child = node.peek_ref_child(field); child)
        visit_node(*child, out, false, all_mode, next_ref_id);
    } else if (fd.wire == Wire::MapOfRef) {
      if (auto child = node.peek_map_child(field); child)
        visit_map(*child, out, all_mode, next_ref_id);
    } else if (fd.wire == Wire::ArrayOfRef || fd.wire == Wire::ArrayOfNumber ||
               fd.wire == Wire::ArrayOfString) {
      if (auto child = node.peek_array_child(field); child)
        visit_array(*child, out, all_mode, next_ref_id);
    }
  }
}

void visit_map(MapNode &map, std::string &out, bool all_mode,
               int &next_ref_id) {
  bool has_changes =
      all_mode || !map.dirty_added().empty() || !map.dirty_removed().empty();

  if (has_changes) {
    encode_uint8(out, kSwitchToStructure);
    encode_number(out, ensure_map_ref_id(map, next_ref_id));

    if (all_mode) {
      for (int idx = 0; idx < static_cast<int>(map.entries().size()); idx++) {
        const auto &entry = map.entries()[idx];
        if (!entry.alive)
          continue;
        encode_uint8(out, kOpAdd);
        encode_number(out, idx);
        encode_string(out, entry.key);
        encode_number(out, ensure_node_ref_id(*entry.value, next_ref_id));
      }
    } else {
      for (int idx : map.dirty_removed()) {
        encode_uint8(out, kOpDelete);
        encode_number(out, idx);
      }
      for (int idx : map.dirty_added()) {
        const auto &entry = map.entries()[idx];
        if (!entry.alive)
          continue;
        encode_uint8(out, kOpAdd);
        encode_number(out, idx);
        encode_string(out, entry.key);
        encode_number(out, ensure_node_ref_id(*entry.value, next_ref_id));
      }
      map.clear_dirty();
    }
  }

  for (const auto &entry : map.entries()) {
    if (entry.alive)
      visit_node(*entry.value, out, false, all_mode, next_ref_id);
  }
}

void visit_array(ArrayNode &arr, std::string &out, bool all_mode,
                 int &next_ref_id) {
  bool has_changes = all_mode || !arr.dirty_added().empty();

  if (has_changes) {
    encode_uint8(out, kSwitchToStructure);
    encode_number(out, ensure_array_ref_id(arr, next_ref_id));

    Wire prim = arr.primitive_wire();
    auto encode_entry_value = [&](const ArrayNode::Entry &entry) {
      if (entry.ref) {
        encode_number(out, ensure_node_ref_id(*entry.ref, next_ref_id));
      } else {
        encode_primitive(
            out, prim == Wire::ArrayOfNumber ? Wire::Number : Wire::String,
            entry.primitive);
      }
    };

    if (all_mode) {
      for (int idx = 0; idx < static_cast<int>(arr.entries().size()); idx++) {
        encode_uint8(out, kOpAdd);
        encode_number(out, idx);
        encode_entry_value(arr.entries()[idx]);
      }
    } else {
      for (int idx : arr.dirty_added()) {
        encode_uint8(out, kOpAdd);
        encode_number(out, idx);
        encode_entry_value(arr.entries()[idx]);
      }
      arr.clear_dirty();
    }
  }

  for (const auto &entry : arr.entries()) {
    if (entry.ref)
      visit_node(*entry.ref, out, false, all_mode, next_ref_id);
  }
}

} // namespace

std::string Encoder::encode_all() {
  std::string out;
  int next = next_ref_id_;
  visit_node(*root_, out, /*is_root=*/true, /*all_mode=*/true, next);
  next_ref_id_ = next;
  return out;
}

std::string Encoder::encode_patch() {
  std::string out;
  visit_node(*root_, out, /*is_root=*/true, /*all_mode=*/false, next_ref_id_);
  return out;
}

namespace {

enum ReflectionClass {
  kReflectionField = 0,
  kReflectionType = 1,
  kReflection = 2
};

const std::vector<ClassDef> &reflection_meta_table() {
  static const std::vector<ClassDef> table = {
      ClassDef{{
          FieldDef{"name", Wire::String, -1},
          FieldDef{"type", Wire::String, -1},
          FieldDef{"referencedType", Wire::Number, -1},
      }},
      ClassDef{{
          FieldDef{"id", Wire::Number, -1},
          FieldDef{"fields", Wire::ArrayOfRef, kReflectionField},
      }},
      ClassDef{{
          FieldDef{"types", Wire::ArrayOfRef, kReflectionType},
          FieldDef{"rootType", Wire::Number, -1},
      }},
  };
  return table;
}

std::string wire_type_name(Wire wire, int &referenced_type_out,
                           bool &has_referenced_type) {
  has_referenced_type = false;
  switch (wire) {
  case Wire::String:
    return "string";
  case Wire::Number:
    return "number";
  case Wire::Boolean:
    return "boolean";
  case Wire::Ref:
    has_referenced_type = true;
    return "ref";
  case Wire::MapOfRef:
    has_referenced_type = true;
    return "map";
  case Wire::ArrayOfRef:
    has_referenced_type = true;
    return "array";
  case Wire::ArrayOfNumber:
    has_referenced_type = true;
    referenced_type_out = -1;
    return "array:number";
  case Wire::ArrayOfString:
    has_referenced_type = true;
    referenced_type_out = -1;
    return "array:string";
  }
  return "";
}

} // namespace

std::string Encoder::encode_reflection(int root_class_id) {
  const auto &meta = reflection_meta_table();
  const auto &game = class_table();

  auto reflection_root = std::make_shared<Node>(kReflection, &meta);
  auto types_array = reflection_root->array_child(0);
  reflection_root->set_number(1, root_class_id);

  for (int class_id = 0; class_id < static_cast<int>(game.size()); class_id++) {
    auto type_node = types_array->push_ref();
    type_node->set_number(0, class_id);
    auto fields_array = type_node->array_child(1);

    for (const FieldDef &fd : game[class_id].fields) {
      auto field_node = fields_array->push_ref();
      field_node->set_string(0, fd.name);

      int referenced_type = fd.ref_class;
      bool has_referenced_type = false;
      std::string type_name =
          wire_type_name(fd.wire, referenced_type, has_referenced_type);
      field_node->set_string(1, type_name);
      if (has_referenced_type) {
        field_node->set_number(2, referenced_type);
      }
    }
  }

  Encoder encoder(reflection_root);
  return encoder.encode_all();
}

} // namespace openkit::colyseus::schema
