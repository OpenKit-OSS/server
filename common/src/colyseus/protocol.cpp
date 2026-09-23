#include "openkit/colyseus/protocol.hpp"

#include <msgpack.hpp>

#include "blueboat/common/msgpack_codec.hpp"

namespace openkit::colyseus {

namespace {

Value unpack_msgpack_object(const msgpack::object &obj) {
  switch (obj.type) {
  case msgpack::type::NIL:
    return nullptr;
  case msgpack::type::BOOLEAN:
    return obj.via.boolean;
  case msgpack::type::POSITIVE_INTEGER:
    return obj.via.u64;
  case msgpack::type::NEGATIVE_INTEGER:
    return obj.via.i64;
  case msgpack::type::FLOAT32:
  case msgpack::type::FLOAT64:
    return obj.via.f64;
  case msgpack::type::STR:
    return std::string(obj.via.str.ptr, obj.via.str.size);
  case msgpack::type::BIN:
    return std::string(obj.via.bin.ptr, obj.via.bin.size);
  case msgpack::type::ARRAY: {
    Value arr = Value::array();
    for (std::uint32_t i = 0; i < obj.via.array.size; i++) {
      arr.push_back(unpack_msgpack_object(obj.via.array.ptr[i]));
    }
    return arr;
  }
  case msgpack::type::MAP: {
    Value map = Value::object();
    for (std::uint32_t i = 0; i < obj.via.map.size; i++) {
      auto &kv = obj.via.map.ptr[i];
      std::string key =
          kv.key.type == msgpack::type::STR
              ? std::string(kv.key.via.str.ptr, kv.key.via.str.size)
              : "";
      map[key] = unpack_msgpack_object(kv.val);
    }
    return map;
  }
  default:
    return nullptr;
  }
}

Value decode_one_msgpack_value(const std::string &bytes, std::size_t &offset) {
  if (offset >= bytes.size()) {
    return nullptr;
  }
  std::size_t off = offset;
  msgpack::object_handle handle =
      msgpack::unpack(bytes.data(), bytes.size(), off);
  offset = off;
  return unpack_msgpack_object(handle.get());
}

void append_length_prefixed(std::string &out, const std::string &value) {
  out.push_back(static_cast<char>(static_cast<std::uint8_t>(value.size())));
  out += value;
}

} // namespace

std::string build_join_room_frame(const std::string &reconnection_token,
                                  const std::string &serializer_id,
                                  const std::string &reflection) {
  std::string frame;
  frame.push_back(static_cast<char>(Opcode::JoinRoom));
  append_length_prefixed(frame, reconnection_token);
  append_length_prefixed(frame, serializer_id);
  if (serializer_id == "schema") {
    frame += reflection;
  }
  return frame;
}

std::string build_room_data_frame(const std::string &type,
                                  const Value &payload) {
  std::string frame;
  frame.push_back(static_cast<char>(Opcode::RoomData));
  frame += blueboat::encode_msgpack(Value(type));
  frame += blueboat::encode_msgpack(payload);
  return frame;
}

std::string build_room_state_frame(const std::string &state_bytes) {
  return std::string(1, static_cast<char>(Opcode::RoomState)) + state_bytes;
}

std::string build_room_state_patch_frame(const std::string &patch_bytes) {
  return std::string(1, static_cast<char>(Opcode::RoomStatePatch)) + patch_bytes;
}

std::string build_leave_room_frame() {
  return std::string(1, static_cast<char>(Opcode::LeaveRoom));
}

std::string build_error_frame(int code, const std::string &message) {
  std::string frame;
  frame.push_back(static_cast<char>(Opcode::Error));
  frame += blueboat::encode_msgpack(Value(code));
  frame += blueboat::encode_msgpack(Value(message));
  return frame;
}

std::string build_ping_frame() {
  return std::string(1, static_cast<char>(Opcode::Ping));
}

ParsedFrame parse_frame(const std::string &bytes) {
  ParsedFrame result;
  if (bytes.empty()) {
    return result;
  }

  std::uint8_t raw_byte = static_cast<std::uint8_t>(bytes[0]);
  auto code = static_cast<Opcode>(raw_byte & kProtocolCodeMask);
  std::size_t offset = 1;

  switch (code) {
  case Opcode::JoinRoom:
    result.kind = ParsedFrame::Kind::JoinAck;
    return result;

  case Opcode::LeaveRoom:
    result.kind = ParsedFrame::Kind::Leave;
    return result;

  case Opcode::Ping:
    result.kind = ParsedFrame::Kind::Ping;
    return result;

  case Opcode::RoomData:
  case Opcode::RoomDataBytes: {
    Value type_value = decode_one_msgpack_value(bytes, offset);
    if (type_value.is_string()) {
      result.type = type_value.get<std::string>();
    } else if (type_value.is_number()) {
      result.type = "i" + std::to_string(type_value.get<long long>());
    } else {
      return result;
    }
    result.payload = decode_one_msgpack_value(bytes, offset);
    result.kind = ParsedFrame::Kind::RoomData;
    return result;
  }

  default:
    return result;
  }
}

} // namespace openkit::colyseus
