#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "blueboat/common/json.hpp"
#include "blueboat/server.hpp"
#include "openkit/colyseus/host.hpp"
#include "openkit/experience_registry.hpp"
#include "openkit/intent_registry.hpp"

namespace openkit {

using blueboat::Value;

struct MatchmakerOptions {
  std::string public_url;

  std::string matchmaker_public_url;

  Value default_questions = Value::array();

  std::string game_source = "original";

  std::string default_map_id;

  std::function<Value(const Value &create_body)> resolve_intent_extras;

  std::optional<blueboat::TlsOptions> tls;
};

class MatchmakerServer {
public:
  MatchmakerServer(blueboat::Server &lobby, IntentRegistry &registry,
                   ExperienceRegistry &experience_registry,
                   MatchmakerOptions options);
  ~MatchmakerServer();

  MatchmakerServer(const MatchmakerServer &) = delete;
  MatchmakerServer &operator=(const MatchmakerServer &) = delete;

  void listen(int port);

private:
  void accept_loop();
  void handle_connection(std::unique_ptr<blueboat::Socket> sock);

  void handle_find_server_to_host_game(blueboat::Socket &sock,
                                       const Value &body,
                                       const std::string &origin);
  void handle_create(blueboat::Socket &sock, const Value &body,
                     const std::string &origin);
  void handle_find_info_from_code(blueboat::Socket &sock, const Value &body,
                                  const std::string &origin);
  void handle_join(blueboat::Socket &sock, const Value &body,
                   const std::string &origin);
  void handle_fetch_source(blueboat::Socket &sock, const std::string &intent_id,
                           const std::string &origin);
  void handle_live_game_summary(blueboat::Socket &sock,
                                const std::string &intent_id,
                                const std::string &origin);
  void handle_map_summary(blueboat::Socket &sock, const std::string &intent_id,
                          const std::string &origin);

  blueboat::Server &lobby_;
  IntentRegistry &registry_;
  ExperienceRegistry &experience_registry_;
  MatchmakerOptions options_;
  std::shared_ptr<blueboat::TlsContext> tls_ctx_;
  std::unique_ptr<colyseus::RoomHost> colyseus_host_;

  int listen_fd_ = -1;
  std::thread accept_thread_;
  std::atomic<bool> shutting_down_{false};
};

} // namespace openkit
