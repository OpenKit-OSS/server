#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "blueboat/common/json.hpp"
#include "blueboat/server.hpp"
#include "openkit/intent_registry.hpp"

namespace openkit {

using blueboat::Value;

struct MatchmakerOptions {
  std::string public_url;

  Value default_questions = Value::array();

  std::string game_source = "original";

  std::optional<blueboat::TlsOptions> tls;
};

class MatchmakerServer {
public:
  MatchmakerServer(blueboat::Server &lobby, IntentRegistry &registry,
                   MatchmakerOptions options);
  ~MatchmakerServer();

  MatchmakerServer(const MatchmakerServer &) = delete;
  MatchmakerServer &operator=(const MatchmakerServer &) = delete;

  void listen(int port);

private:
  void accept_loop();
  void handle_connection(std::unique_ptr<blueboat::Socket> sock);

  void handle_find_server_to_host_game(blueboat::Socket &sock,
                                       const Value &body);
  void handle_create(blueboat::Socket &sock, const Value &body);
  void handle_find_info_from_code(blueboat::Socket &sock, const Value &body);
  void handle_join(blueboat::Socket &sock, const Value &body);
  void handle_fetch_source(blueboat::Socket &sock,
                           const std::string &intent_id);
  void handle_live_game_summary(blueboat::Socket &sock,
                                const std::string &intent_id);

  blueboat::Server &lobby_;
  IntentRegistry &registry_;
  MatchmakerOptions options_;
  std::shared_ptr<blueboat::TlsContext> tls_ctx_;

  int listen_fd_ = -1;
  std::thread accept_thread_;
  std::atomic<bool> shutting_down_{false};
};

} // namespace openkit
