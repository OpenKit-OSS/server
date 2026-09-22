#include "openkit/matchmaker.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <stdexcept>

#include "blueboat/common/http.hpp"
#include "blueboat/common/socket.hpp"
#include "blueboat/common/tcp.hpp"
#include "blueboat/common/tls_socket.hpp"

namespace openkit {

namespace {

void send_json(blueboat::Socket &sock, int status, const Value &body) {
  blueboat::http::write_response(sock, status, status == 200 ? "OK" : "Error",
                                 "application/json", body.dump(),
                                 {{"Access-Control-Allow-Origin", "*"}});
}

} // namespace

MatchmakerServer::MatchmakerServer(blueboat::Server &lobby,
                                   IntentRegistry &registry,
                                   MatchmakerOptions options)
    : lobby_(lobby), registry_(registry), options_(std::move(options)) {
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

  if (request->method == "OPTIONS") {
    blueboat::http::write_response(
        *sock, 204, "No Content", "text/plain", "",
        {
            {"Access-Control-Allow-Origin", "*"},
            {"Access-Control-Allow-Methods", "POST, OPTIONS"},
            {"Access-Control-Allow-Headers", "Content-Type"},
        });
    return;
  }

  Value body = Value::object();
  try {
    if (!request->body.empty()) {
      body = Value::parse(request->body);
    }
  } catch (const std::exception &) {
    send_json(*sock, 400, Value{{"message", "invalid JSON body"}});
    return;
  }

  static const std::string fetch_source_prefix =
      "/api/matchmaker/intent/fetch-source/";
  static const std::string live_game_summary_prefix =
      "/api/matchmaker/intent/live-game/summary/";

  if (request->path == "/api/matchmaker/find-server-to-host-game") {
    handle_find_server_to_host_game(*sock, body);
  } else if (request->path == "/api/matchmaker/intent/live-game/create") {
    handle_create(*sock, body);
  } else if (request->path == "/api/matchmaker/find-info-from-code") {
    handle_find_info_from_code(*sock, body);
  } else if (request->path == "/api/matchmaker/join") {
    handle_join(*sock, body);
  } else if (request->path.rfind(fetch_source_prefix, 0) == 0) {
    handle_fetch_source(*sock,
                        request->path.substr(fetch_source_prefix.size()));
  } else if (request->path.rfind(live_game_summary_prefix, 0) == 0) {
    handle_live_game_summary(
        *sock, request->path.substr(live_game_summary_prefix.size()));
  } else {
    send_json(*sock, 404, Value{{"message", "not found"}});
  }
}

void MatchmakerServer::handle_find_server_to_host_game(blueboat::Socket &sock,
                                                       const Value &) {
  send_json(sock, 200, Value{{"url", options_.public_url}});
}

void MatchmakerServer::handle_create(blueboat::Socket &sock,
                                     const Value &body) {
  Value game_options = body.value("gameOptions", Value::object());
  if (game_options.contains("gameGoal") &&
      game_options.at("gameGoal").is_object()) {
    const Value &game_goal = game_options.at("gameGoal");
    game_options["goal"] =
        Value{{"type", game_goal.value("goal", std::string())},
              {"value", game_goal.value("value", 0LL)}};
    game_options.erase("gameGoal");
  }

  std::string code = registry_.reserve_code();

  Value intent_data{
      {"gameOptions", game_options},
      {"questions", options_.default_questions},
      {"gameCode", code},
  };
  std::string intent_id = registry_.register_intent(intent_data);

  blueboat::http::write_response(sock, 200, "OK", "text/plain", intent_id,
                                 {{"Access-Control-Allow-Origin", "*"}});
}

void MatchmakerServer::handle_find_info_from_code(blueboat::Socket &sock,
                                                  const Value &body) {
  std::string code = body.value("code", std::string());

  auto room_id = registry_.resolve_code(code);
  if (!room_id) {
    send_json(sock, 404, Value{{"message", "no game found for that code"}});
    return;
  }

  send_json(sock, 200,
            Value{{"roomId", *room_id}, {"useRandomNamePicker", false}});
}

void MatchmakerServer::handle_join(blueboat::Socket &sock, const Value &body) {
  std::string room_id = body.value("roomId", std::string());
  if (room_id.empty()) {
    send_json(sock, 400, Value{{"message", "roomId required"}});
    return;
  }

  auto rooms = lobby_.get_rooms();
  bool exists = std::any_of(
      rooms.begin(), rooms.end(),
      [&](const blueboat::RoomSnapshot &room) { return room.id == room_id; });
  if (!exists) {
    send_json(sock, 404, Value{{"message", "room not found"}});
    return;
  }

  Value intent_data{{"name", body.value("name", std::string())}};
  std::string intent_id = registry_.register_intent(intent_data);

  send_json(sock, 200,
            Value{
                {"source", "original"},
                {"serverUrl", options_.public_url},
                {"roomId", room_id},
                {"intentId", intent_id},
            });
}

void MatchmakerServer::handle_fetch_source(blueboat::Socket &sock,
                                           const std::string &intent_id) {
  if (!registry_.resolve_intent(intent_id)) {
    blueboat::http::write_response(sock, 404, "Not Found", "text/plain",
                                   "unknown intent",
                                   {{"Access-Control-Allow-Origin", "*"}});
    return;
  }
  blueboat::http::write_response(sock, 200, "OK", "text/plain",
                                 options_.game_source,
                                 {{"Access-Control-Allow-Origin", "*"}});
}

void MatchmakerServer::handle_live_game_summary(blueboat::Socket &sock,
                                                const std::string &intent_id) {
  auto intent_data = registry_.resolve_intent(intent_id);
  if (!intent_data) {
    send_json(sock, 404, Value{{"message", "unknown intent"}});
    return;
  }
  send_json(
      sock, 200,
      Value{{"questions", intent_data->value("questions", Value::array())},
            {"usingGroups", false}});
}

} // namespace openkit
