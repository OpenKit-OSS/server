#pragma once

#include <optional>
#include <string>

namespace openkit::colyseus {

std::optional<std::string>
decode_user_id_from_auth_token(const std::string &auth_token);

} // namespace openkit::colyseus
