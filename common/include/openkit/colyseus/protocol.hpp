#pragma once

#include <cstdint>
#include <string>

#include "blueboat/common/json.hpp"

namespace openkit::colyseus {

using Value = blueboat::Value;

enum class Opcode : std::uint8_t {
  JoinRoom = 10,
  Error = 11,
  LeaveRoom = 12,
  RoomData = 13,
  RoomState = 14,
  RoomStatePatch = 15,
  RoomDataBytes = 17,
  Ping = 18,
};

constexpr std::uint8_t kProtocolCodeMask = 0x1F;
constexpr std::uint8_t kProtocolModifierMask = 0xE0;

std::string build_join_room_frame(const std::string &reconnection_token,
                                  const std::string &serializer_id = "none",
                                  const std::string &reflection = "");

std::string build_room_data_frame(const std::string &type,
                                  const Value &payload = Value());

std::string build_room_state_frame(const std::string &state_bytes);
std::string build_room_state_patch_frame(const std::string &patch_bytes);

std::string build_leave_room_frame();
std::string build_error_frame(int code, const std::string &message);
std::string build_ping_frame();

struct ParsedFrame {
  enum class Kind {
    JoinAck,
    Leave,
    RoomData,
    Ping,
    Unknown,
  };

  Kind kind = Kind::Unknown;
  std::string type;
  Value payload;
};

ParsedFrame parse_frame(const std::string &bytes);

} // namespace openkit::colyseus
