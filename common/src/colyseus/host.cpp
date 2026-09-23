#include "openkit/colyseus/host.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

#include "blueboat/common/random_id.hpp"
#include "blueboat/common/ws_connection.hpp"
#include "blueboat/common/ws_handshake.hpp"
#include "openkit/colyseus/auth_token.hpp"
#include "openkit/colyseus/map_room.hpp"
#include "openkit/colyseus/protocol.hpp"

namespace openkit::colyseus {

namespace {

constexpr const char *kCreatePrefix = "/matchmake/create/";
constexpr const char *kJoinByIdPrefix = "/matchmake/joinById/";

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

bool header_contains_token(const std::string &value, const std::string &token) {
  return to_lower(value).find(to_lower(token)) != std::string::npos;
}

std::string iso8601_now() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
  char full[40];
  std::snprintf(full, sizeof(full), "%s.%03dZ", buf,
                static_cast<int>(ms.count()));
  return std::string(full);
}

std::map<std::string, std::string> cors_headers(const std::string &origin) {
  return {
      {"Access-Control-Allow-Origin", origin.empty() ? "*" : origin},
      {"Access-Control-Allow-Credentials", "true"},
  };
}

void send_json(blueboat::Socket &sock, int status, const Value &body,
               const std::string &origin) {
  blueboat::http::write_response(sock, status, status == 200 ? "OK" : "Error",
                                 "application/json", body.dump(),
                                 cors_headers(origin));
}

Value build_room_response(const std::string &process_id,
                          const std::string &room_name,
                          const std::string &room_id,
                          const std::string &session_id, int clients) {
  return Value{
      {"room",
       Value{
           {"clients", clients},
           {"locked", false},
           {"private", false},
           {"maxClients", nullptr},
           {"unlisted", false},
           {"createdAt", iso8601_now()},
           {"name", room_name},
           {"processId", process_id},
           {"roomId", room_id},
       }},
      {"sessionId", session_id},
  };
}

} // namespace

RoomHost::RoomHost(IntentRegistry &intent_registry,
                   ExperienceRegistry &experience_registry,
                   std::string process_id, std::string default_map_id)
    : intent_registry_(intent_registry),
      experience_registry_(experience_registry),
      process_id_(std::move(process_id)),
      default_map_id_(std::move(default_map_id)) {}

std::optional<std::string>
RoomHost::resolve_map_id(const std::string &experience_id) const {
  if (auto info = experience_registry_.find(experience_id)) {
    return info->map_id;
  }
  if (!default_map_id_.empty()) {
    return default_map_id_;
  }
  return std::nullopt;
}

const MapCatalog &RoomHost::catalog_for_map(const std::string &map_id) {
  auto it = map_catalog_cache_.find(map_id);
  if (it != map_catalog_cache_.end()) {
    return it->second;
  }
  auto [inserted, ok] =
      map_catalog_cache_.emplace(map_id, MapCatalog::load(map_id));
  return inserted->second;
}

bool RoomHost::has_room(const std::string &room_id) const {
  std::lock_guard<std::mutex> guard(mutex_);
  return rooms_.count(room_id) != 0;
}

bool RoomHost::is_create_request(const blueboat::http::Request &request) const {
  return request.method == "POST" && request.path.rfind(kCreatePrefix, 0) == 0;
}

bool RoomHost::is_join_by_id_request(
    const blueboat::http::Request &request) const {
  return request.method == "POST" &&
         request.path.rfind(kJoinByIdPrefix, 0) == 0;
}

bool RoomHost::is_room_websocket_request(
    const blueboat::http::Request &request) const {
  if (request.method != "GET")
    return false;
  auto upgrade_it = request.headers.find("upgrade");
  if (upgrade_it == request.headers.end() ||
      !header_contains_token(upgrade_it->second, "websocket"))
    return false;
  return room_id_for_path(request.path).has_value();
}

std::optional<std::string>
RoomHost::room_id_for_path(const std::string &path) const {
  std::string prefix = "/" + process_id_ + "/";
  if (path.rfind(prefix, 0) != 0)
    return std::nullopt;
  std::string room_id = path.substr(prefix.size());
  if (room_id.empty() || room_id.find('/') != std::string::npos)
    return std::nullopt;
  return room_id;
}

void RoomHost::handle_create(blueboat::Socket &sock,
                             const blueboat::http::Request &request) {
  std::string origin =
      request.headers.count("origin") ? request.headers.at("origin") : "";
  std::string room_name =
      request.path.substr(std::string(kCreatePrefix).size());

  if (room_name != "MapRoom") {
    send_json(
        sock, 404,
        Value{{"code", 4210}, {"message", "no room handler for " + room_name}},
        origin);
    return;
  }

  Value create_options = Value::object();
  try {
    if (!request.body.empty()) {
      create_options = Value::parse(request.body);
    }
  } catch (const std::exception &) {
    send_json(sock, 400, Value{{"message", "invalid JSON body"}}, origin);
    return;
  }

  std::string intent_id = create_options.value("intentId", std::string());
  auto intent = intent_registry_.resolve_intent(intent_id);
  std::string experience_id =
      intent ? intent->value("experienceId", std::string()) : std::string();
  auto map_id = resolve_map_id(experience_id);
  if (!map_id) {
    send_json(sock, 404,
              Value{{"code", 4210},
                    {"message",
                     "no map configured for experienceId " + experience_id}},
              origin);
    return;
  }

  std::unique_ptr<Room> room;
  try {
    room =
        std::make_unique<MapRoom>(intent_registry_, catalog_for_map(*map_id));
  } catch (const std::exception &e) {
    send_json(sock, 500, Value{{"code", 4213}, {"message", e.what()}}, origin);
    return;
  }
  std::string room_id = blueboat::random_id();
  std::string session_id = blueboat::random_id();

  RoomInitOptions init;
  init.room_id = room_id;
  init.room_type = room_name;
  init.on_room_disposed = [this](const std::string &id) {
    std::lock_guard<std::mutex> guard(mutex_);
    rooms_.erase(id);
  };

  try {
    room->initialize(init, create_options);
  } catch (const std::exception &e) {
    send_json(sock, 500, Value{{"code", 4213}, {"message", e.what()}}, origin);
    return;
  }

  std::string game_code =
      intent ? intent->value("gameCode", std::string()) : std::string();
  if (!game_code.empty()) {
    intent_registry_.link_code_to_room(game_code, room_id);
  }

  std::string creator_client_id =
      decode_user_id_from_auth_token(
          create_options.value("authToken", std::string()))
          .value_or(session_id);

  {
    std::lock_guard<std::mutex> guard(mutex_);
    pending_sessions_[session_id] =
        PendingReservation{room_id, create_options, creator_client_id};
    rooms_[room_id] = std::move(room);
  }

  send_json(sock, 200,
            build_room_response(process_id_, room_name, room_id, session_id, 1),
            origin);
}

void RoomHost::handle_join_by_id(blueboat::Socket &sock,
                                 const blueboat::http::Request &request) {
  std::string origin =
      request.headers.count("origin") ? request.headers.at("origin") : "";
  std::string room_id =
      request.path.substr(std::string(kJoinByIdPrefix).size());

  Value join_options = Value::object();
  try {
    if (!request.body.empty()) {
      join_options = Value::parse(request.body);
    }
  } catch (const std::exception &) {
    send_json(sock, 400, Value{{"message", "invalid JSON body"}}, origin);
    return;
  }

  std::string join_intent_id = join_options.value("intentId", std::string());
  if (auto join_intent = intent_registry_.resolve_intent(join_intent_id)) {
    std::string name = join_intent->value("name", std::string());
    if (!name.empty()) {
      join_options["name"] = name;
    }
  }

  std::string session_id = blueboat::random_id();
  std::size_t client_count = 0;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
      send_json(sock, 404, Value{{"code", 4212}, {"message", "room not found"}},
                origin);
      return;
    }
    client_count = room_it->second->client_count();
    pending_sessions_[session_id] =
        PendingReservation{room_id, join_options, session_id};
  }

  send_json(sock, 200,
            build_room_response(process_id_, "MapRoom", room_id, session_id,
                                static_cast<int>(client_count) + 1),
            origin);
}

void RoomHost::handle_websocket(std::unique_ptr<blueboat::Socket> sock,
                                const blueboat::http::Request &request) {
  auto room_id_opt = room_id_for_path(request.path);
  auto key_it = request.headers.find("sec-websocket-key");
  if (!room_id_opt || key_it == request.headers.end()) {
    blueboat::http::write_response(*sock, 400, "Bad Request", "text/plain",
                                   "Expected a WebSocket upgrade request");
    return;
  }
  std::string room_id = *room_id_opt;

  auto query = blueboat::http::parse_query(request.query);
  std::string session_id = query.count("sessionId") ? query["sessionId"] : "";

  Room *room = nullptr;
  Value reservation_options = Value::object();
  std::string client_id;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    auto room_it = rooms_.find(room_id);
    auto reservation_it = pending_sessions_.find(session_id);
    if (room_it == rooms_.end() || reservation_it == pending_sessions_.end() ||
        reservation_it->second.room_id != room_id) {
      blueboat::http::write_response(*sock, 404, "Not Found", "text/plain",
                                     "unknown room or session");
      return;
    }
    room = room_it->second.get();
    reservation_options = reservation_it->second.options;
    client_id = reservation_it->second.client_id;
    pending_sessions_.erase(reservation_it);
  }

  std::string accept = blueboat::ws_handshake::compute_accept(key_it->second);
  std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                         "Upgrade: websocket\r\n"
                         "Connection: Upgrade\r\n"
                         "Sec-WebSocket-Accept: " +
                         accept + "\r\n\r\n";
  if (sock->write(response.data(), response.size()) < 0) {
    return;
  }

  blueboat::WsConnection ws(std::move(sock), /*is_client=*/false);
  ws.send_binary(build_join_room_frame("", room->serializer_id(),
                                       room->cached_reflection()));

  ws.set_message_handler([&](bool /*is_binary*/, const std::string &payload) {
    ParsedFrame frame = parse_frame(payload);
    switch (frame.kind) {
    case ParsedFrame::Kind::JoinAck:
      break;
    case ParsedFrame::Kind::Leave:
      room->remove_client(session_id, true);
      ws.close();
      break;
    case ParsedFrame::Kind::RoomData:
      room->dispatch_message(session_id, frame.type, frame.payload);
      break;
    case ParsedFrame::Kind::Ping:
      ws.send_binary(build_ping_frame());
      break;
    case ParsedFrame::Kind::Unknown:
      break;
    }
  });

  ws.set_close_handler([&] { room->remove_client(session_id, false); });

  room->add_client(session_id, client_id, &ws, reservation_options);
  ws.run_recv_loop();
}

} // namespace openkit::colyseus
