#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "blueboat/common/json.hpp"

namespace openkit::colyseus::schema {

using Value = blueboat::Value;

enum class Wire {
  String,
  Number,
  Boolean,
  Ref,
  MapOfRef,
  ArrayOfRef,
  ArrayOfNumber,
  ArrayOfString,
};

struct FieldDef {
  std::string name;
  Wire wire;
  int ref_class; // -1 when not applicable
};

struct ClassDef {
  std::vector<FieldDef> fields;
};

const std::vector<ClassDef> &class_table();

class MapNode;
class ArrayNode;

class Node : public std::enable_shared_from_this<Node> {
public:
  explicit Node(int class_id,
                const std::vector<ClassDef> *table = &class_table());

  int class_id() const { return class_id_; }
  int ref_id() const { return ref_id_; }
  void set_ref_id(int id) { ref_id_ = id; }
  bool has_ref_id() const { return ref_id_ >= 0; }

  const ClassDef &def() const { return (*table_)[class_id_]; }

  void set_string(int field, const std::string &value);
  void set_number(int field, double value);
  void set_bool(int field, bool value);

  const std::string &get_string(int field) const;
  double get_number(int field) const;
  bool get_bool(int field) const;

  std::shared_ptr<Node> ref_child(int field);
  std::shared_ptr<MapNode> map_child(int field);
  std::shared_ptr<ArrayNode> array_child(int field);

  std::shared_ptr<Node> peek_ref_child(int field) const { return refs_[field]; }
  std::shared_ptr<MapNode> peek_map_child(int field) const {
    return maps_[field];
  }
  std::shared_ptr<ArrayNode> peek_array_child(int field) const {
    return arrays_[field];
  }

  const std::set<int> &dirty() const { return dirty_; }
  void clear_dirty() { dirty_.clear(); }
  const std::set<int> &all_touched() const { return all_touched_; }
  void touch(int field) {
    dirty_.insert(field);
    all_touched_.insert(field);
  }

private:
  int class_id_;
  const std::vector<ClassDef> *table_;
  int ref_id_ = -1;
  std::vector<Value> primitives_;
  std::vector<std::shared_ptr<Node>> refs_;
  std::vector<std::shared_ptr<MapNode>> maps_;
  std::vector<std::shared_ptr<ArrayNode>> arrays_;
  std::set<int> dirty_;
  std::set<int> all_touched_;
};

class MapNode {
public:
  explicit MapNode(int child_class,
                   const std::vector<ClassDef> *table = &class_table())
      : child_class_(child_class), table_(table) {}

  int child_class() const { return child_class_; }
  int ref_id() const { return ref_id_; }
  void set_ref_id(int id) { ref_id_ = id; }
  bool has_ref_id() const { return ref_id_ >= 0; }

  std::shared_ptr<Node> get_or_create(const std::string &key);
  std::shared_ptr<Node> find(const std::string &key) const;
  void erase(const std::string &key);

  struct Entry {
    std::string key;
    std::shared_ptr<Node> value;
    bool alive = true;
  };
  const std::vector<Entry> &entries() const { return entries_; }

  const std::set<int> &dirty_added() const { return dirty_added_; }
  const std::set<int> &dirty_removed() const { return dirty_removed_; }
  void clear_dirty() {
    dirty_added_.clear();
    dirty_removed_.clear();
  }

private:
  int child_class_;
  const std::vector<ClassDef> *table_;
  int ref_id_ = -1;
  std::vector<Entry> entries_;
  std::map<std::string, int> index_by_key_;
  std::set<int> dirty_added_;
  std::set<int> dirty_removed_;
};

class ArrayNode {
public:
  explicit ArrayNode(int child_class, Wire primitive_wire = Wire::ArrayOfRef,
                     const std::vector<ClassDef> *table = &class_table())
      : child_class_(child_class), primitive_wire_(primitive_wire),
        table_(table) {}

  int child_class() const { return child_class_; }
  int ref_id() const { return ref_id_; }
  void set_ref_id(int id) { ref_id_ = id; }
  bool has_ref_id() const { return ref_id_ >= 0; }
  Wire primitive_wire() const { return primitive_wire_; }

  std::shared_ptr<Node> push_ref();
  void push_primitive(const Value &v);
  std::size_t size() const { return values_.size(); }

  struct Entry {
    std::shared_ptr<Node> ref; // null for primitive entries
    Value primitive;
  };
  const std::vector<Entry> &entries() const { return values_; }

  const std::set<int> &dirty_added() const { return dirty_added_; }
  void clear_dirty() { dirty_added_.clear(); }

private:
  int child_class_;
  Wire primitive_wire_;
  const std::vector<ClassDef> *table_;
  int ref_id_ = -1;
  std::vector<Entry> values_;
  std::set<int> dirty_added_;
};

class Encoder {
public:
  explicit Encoder(std::shared_ptr<Node> root) : root_(std::move(root)) {}

  std::string encode_all();
  std::string encode_patch();

  static std::string encode_reflection(int root_class_id);

private:
  std::shared_ptr<Node> root_;
  int next_ref_id_ = 1;

  int ensure_ref_id(int current);
};

} // namespace openkit::colyseus::schema
