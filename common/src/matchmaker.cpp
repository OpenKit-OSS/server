#include "openkit/matchmaker.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <stdexcept>

#include "blueboat/common/http.hpp"
#include "blueboat/common/random_id.hpp"
#include "blueboat/common/socket.hpp"
#include "blueboat/common/tcp.hpp"
#include "blueboat/common/tls_socket.hpp"

namespace openkit {

namespace {

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

} // namespace

MatchmakerServer::MatchmakerServer(blueboat::Server &lobby,
                                   IntentRegistry &registry,
                                   ExperienceRegistry &experience_registry,
                                   MatchmakerOptions options)
    : lobby_(lobby), registry_(registry),
      experience_registry_(experience_registry), options_(std::move(options)),
      colyseus_host_(std::make_unique<colyseus::RoomHost>(
          registry_, experience_registry_, blueboat::random_id(9),
          options_.default_map_id)) {
  if (options_.tls) {
    tls_ctx_ = blueboat::TlsContext::create_server(options_.tls->cert_file,
                                                   options_.tls->key_file);
  }
}

MatchmakerServer::~MatchmakerServer() {
  shutting_down_.store(true);
  if (listen_fd_ >= 0) {
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
}

void MatchmakerServer::listen(int port) {
  listen_fd_ = blueboat::tcp::listen_on(port);
  if (listen_fd_ < 0) {
    throw std::runtime_error("openkit: matchmaker failed to bind port " +
                             std::to_string(port));
  }
  accept_thread_ = std::thread([this] { accept_loop(); });
}

void MatchmakerServer::accept_loop() {
  while (!shutting_down_.load()) {
    int fd = blueboat::tcp::accept_connection(listen_fd_);
    if (fd < 0) {
      continue;
    }

    std::unique_ptr<blueboat::Socket> sock;
    if (tls_ctx_) {
      auto tls_sock = std::make_unique<blueboat::TlsSocket>(fd, tls_ctx_);
      if (!tls_sock->accept_server()) {
        continue;
      }
      sock = std::move(tls_sock);
    } else {
      sock = std::make_unique<blueboat::PlainSocket>(fd);
    }

    std::thread(&MatchmakerServer::handle_connection, this, std::move(sock))
        .detach();
  }
}

void MatchmakerServer::handle_connection(
    std::unique_ptr<blueboat::Socket> sock) {
  auto request = blueboat::http::read_request(*sock);
  if (!request) {
    return;
  }

  std::string origin =
      request->headers.count("origin") ? request->headers.at("origin") : "";

  if (request->method == "OPTIONS") {
    auto headers = cors_headers(origin);
    headers["Access-Control-Allow-Methods"] = "POST, OPTIONS";
    headers["Access-Control-Allow-Headers"] = "Content-Type";
    blueboat::http::write_response(*sock, 204, "No Content", "text/plain", "",
                                   headers);
    return;
  }

  if (colyseus_host_->is_room_websocket_request(*request)) {
    colyseus_host_->handle_websocket(std::move(sock), *request);
    return;
  }

  if (colyseus_host_->is_create_request(*request)) {
    colyseus_host_->handle_create(*sock, *request);
    return;
  }

  if (colyseus_host_->is_join_by_id_request(*request)) {
    colyseus_host_->handle_join_by_id(*sock, *request);
    return;
  }

  Value body = Value::object();
  try {
    if (!request->body.empty()) {
      body = Value::parse(request->body);
    }
  } catch (const std::exception &) {
    send_json(*sock, 400, Value{{"message", "invalid JSON body"}}, origin);
    return;
  }

  static const std::string fetch_source_prefix =
      "/api/matchmaker/intent/fetch-source/";
  static const std::string live_game_summary_prefix =
      "/api/matchmaker/intent/live-game/summary/";
  static const std::string map_summary_prefix =
      "/api/matchmaker/intent/map/summary/";

  if (request->path == "/api/matchmaker/find-server-to-host-game") {
    handle_find_server_to_host_game(*sock, body, origin);
  } else if (request->path == "/api/matchmaker/intent/live-game/create" ||
             request->path == "/api/matchmaker/intent/map/play/create") {
    handle_create(*sock, body, origin);
  } else if (request->path == "/api/matchmaker/find-info-from-code") {
    handle_find_info_from_code(*sock, body, origin);
  } else if (request->path == "/api/matchmaker/join") {
    handle_join(*sock, body, origin);
  } else if (request->path.rfind(fetch_source_prefix, 0) == 0) {
    handle_fetch_source(*sock, request->path.substr(fetch_source_prefix.size()),
                        origin);
  } else if (request->path.rfind(live_game_summary_prefix, 0) == 0) {
    handle_live_game_summary(
        *sock, request->path.substr(live_game_summary_prefix.size()), origin);
  } else if (request->path.rfind(map_summary_prefix, 0) == 0) {
    handle_map_summary(*sock, request->path.substr(map_summary_prefix.size()),
                       origin);
  } else {
    send_json(*sock, 404, Value{{"message", "not found"}}, origin);
  }
}

void MatchmakerServer::handle_find_server_to_host_game(
    blueboat::Socket &sock, const Value &body, const std::string &origin) {
  bool is_map = body.value("source", std::string()) == "map";
  const std::string &url = (is_map && !options_.matchmaker_public_url.empty())
                               ? options_.matchmaker_public_url
                               : options_.public_url;
  send_json(sock, 200, Value{{"url", url}}, origin);
}

void MatchmakerServer::handle_create(blueboat::Socket &sock, const Value &body,
                                     const std::string &origin) {
  Value game_options = body.contains("gameOptions")
                           ? body.at("gameOptions")
                           : body.value("options", Value::object());
  if (game_options.contains("gameGoal") &&
      game_options.at("gameGoal").is_object()) {
    const Value &game_goal = game_options.at("gameGoal");
    game_options["goal"] =
        Value{{"type", game_goal.value("goal", std::string())},
              {"value", game_goal.value("value", 0LL)}};
    game_options.erase("gameGoal");
  }

  std::string code = registry_.reserve_code();

  Value extras = options_.resolve_intent_extras
                     ? options_.resolve_intent_extras(body)
                     : Value::object();

  Value intent_data{
      {"gameOptions", game_options},
      {"questions", options_.default_questions},
      {"gameCode", code},
      {"extras", extras},
      {"experienceId", body.value("experienceId", std::string())},
  };
  std::string intent_id = registry_.register_intent(intent_data);

  blueboat::http::write_response(sock, 200, "OK", "text/plain", intent_id,
                                 cors_headers(origin));
}

void MatchmakerServer::handle_find_info_from_code(blueboat::Socket &sock,
                                                  const Value &body,
                                                  const std::string &origin) {
  std::string code = body.value("code", std::string());

  auto room_id = registry_.resolve_code(code);
  if (!room_id) {
    send_json(sock, 404, Value{{"message", "no game found for that code"}},
              origin);
    return;
  }

  send_json(sock, 200,
            Value{{"roomId", *room_id}, {"useRandomNamePicker", false}},
            origin);
}

void MatchmakerServer::handle_join(blueboat::Socket &sock, const Value &body,
                                   const std::string &origin) {
  std::string room_id = body.value("roomId", std::string());
  if (room_id.empty()) {
    send_json(sock, 400, Value{{"message", "roomId required"}}, origin);
    return;
  }

  auto rooms = lobby_.get_rooms();
  bool is_blueboat_room = std::any_of(
      rooms.begin(), rooms.end(),
      [&](const blueboat::RoomSnapshot &room) { return room.id == room_id; });
  bool is_map_room = !is_blueboat_room && colyseus_host_->has_room(room_id);
  if (!is_blueboat_room && !is_map_room) {
    send_json(sock, 404, Value{{"message", "room not found"}}, origin);
    return;
  }

  Value intent_data{{"name", body.value("name", std::string())}};
  std::string intent_id = registry_.register_intent(intent_data);

  const std::string &server_url =
      is_map_room && !options_.matchmaker_public_url.empty()
          ? options_.matchmaker_public_url
          : options_.public_url;

  send_json(sock, 200,
            Value{
                {"source", is_map_room ? "map" : "original"},
                {"serverUrl", server_url},
                {"roomId", room_id},
                {"intentId", intent_id},
            },
            origin);
}

namespace {
std::optional<std::string>
resolve_map_id_for_experience(ExperienceRegistry &experience_registry,
                              const std::string &experience_id,
                              const std::string &default_map_id) {
  if (auto info = experience_registry.find(experience_id)) {
    return info->map_id;
  }
  if (!default_map_id.empty()) {
    return default_map_id;
  }
  return std::nullopt;
}
} // namespace

void MatchmakerServer::handle_fetch_source(blueboat::Socket &sock,
                                           const std::string &intent_id,
                                           const std::string &origin) {
  auto intent = registry_.resolve_intent(intent_id);
  if (!intent) {
    blueboat::http::write_response(sock, 404, "Not Found", "text/plain",
                                   "unknown intent", cors_headers(origin));
    return;
  }
  std::string experience_id = intent->value("experienceId", std::string());
  auto map_id = resolve_map_id_for_experience(
      experience_registry_, experience_id, options_.default_map_id);
  blueboat::http::write_response(
      sock, 200, "OK", "text/plain",
      map_id ? std::string("map") : options_.game_source, cors_headers(origin));
}

void MatchmakerServer::handle_map_summary(blueboat::Socket &sock,
                                          const std::string &intent_id,
                                          const std::string &origin) {
  auto intent = registry_.resolve_intent(intent_id);
  if (!intent) {
    send_json(sock, 404, Value{{"message", "unknown intent"}}, origin);
    return;
  }
  std::string experience_id = intent->value("experienceId", std::string());
  auto map_id = resolve_map_id_for_experience(
      experience_registry_, experience_id, options_.default_map_id);
  if (!map_id) {
    send_json(sock, 404,
              Value{{"message", "no map configured for this experience"}},
              origin);
    return;
  }
  send_json(sock, 200, Value{{"mapId", *map_id}}, origin);
}

void MatchmakerServer::handle_live_game_summary(blueboat::Socket &sock,
                                                const std::string &intent_id,
                                                const std::string &origin) {
  auto intent_data = registry_.resolve_intent(intent_id);
  if (!intent_data) {
    send_json(sock, 404, Value{{"message", "unknown intent"}}, origin);
    return;
  }
  send_json(
      sock, 200,
      Value{{"questions", intent_data->value("questions", Value::array())},
            {"usingGroups", false}},
      origin);
}

} // namespace openkit
