#include "openkit/colyseus/auth_token.hpp"

#include <array>
#include <cstdint>

#include "blueboat/common/json.hpp"

namespace openkit::colyseus {

namespace {

std::optional<std::string> base64url_decode(const std::string &input) {
  static const std::array<int8_t, 256> table = [] {
    std::array<int8_t, 256> t{};
    t.fill(-1);
    const char *alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    for (int i = 0; i < 64; i++)
      t[static_cast<unsigned char>(alphabet[i])] = static_cast<int8_t>(i);
    return t;
  }();

  std::string out;
  int buffer = 0;
  int bits = 0;
  for (char c : input) {
    int8_t value = table[static_cast<unsigned char>(c)];
    if (value < 0)
      continue;
    buffer = (buffer << 6) | value;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
    }
  }
  return out;
}

} // namespace

std::optional<std::string>
decode_user_id_from_auth_token(const std::string &auth_token) {
  std::size_t first_dot = auth_token.find('.');
  if (first_dot == std::string::npos)
    return std::nullopt;
  std::size_t second_dot = auth_token.find('.', first_dot + 1);
  if (second_dot == std::string::npos)
    return std::nullopt;

  std::string payload_segment =
      auth_token.substr(first_dot + 1, second_dot - first_dot - 1);
  auto decoded = base64url_decode(payload_segment);
  if (!decoded)
    return std::nullopt;

  try {
    blueboat::Value payload = blueboat::Value::parse(*decoded);
    std::string id = payload.value("_id", std::string());
    if (id.empty())
      return std::nullopt;
    return id;
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

} // namespace openkit::colyseus
