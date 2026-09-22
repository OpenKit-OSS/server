#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "blueboat/memory_pubsub.hpp"
#include "blueboat/memory_storage.hpp"
#include "blueboat/server.hpp"
#include "openkit/catalog.hpp"
#include "openkit/gamemode_registry.hpp"
#include "openkit/intent_registry.hpp"
#include "openkit/matchmaker.hpp"
#include "openkit/questions.hpp"
#include "openkit/tycoon_room.hpp"

namespace {

std::unique_ptr<blueboat::Server> g_server;

void handle_signal(int) {
  if (g_server) {
    g_server->shutdown();
  }
}

void print_usage(const char *argv0) {
  std::cerr << "Usage: " << argv0
            << " --questions <file.json> [--gamemode <name>] [--port <port>]\n"
               "       [--tls-cert <file> --tls-key <file>]\n\n"
            << "  --questions        Path to a JSON question file in the simple format:\n"
            << "                       [{ \"text\": \"2+2?\", \"answers\": [\"4\", \"5\"], \"correct\": [0] }]\n"
            << "                     \"correct\" may be a single index or a list of indices, and\n"
            << "                     defaults to [0].\n"
            << "  --gamemode         Which gamemode to host (default: tycoon)\n"
            << "  --port             TCP port to listen on (default: 4000)\n"
            << "  --tls-cert/--tls-key\n"
            << "                     PEM cert/key to terminate TLS (wss://) directly, e.g. from\n"
            << "                     mkcert. Required together; plain ws:// is used otherwise.\n"
            << "  --matchmaker-port  Also run a matchmaker HTTP server on this port,\n"
            << "                     implementing gimkit's create/find-info-from-code/join\n"
            << "                     endpoints against this lobby server. Reuses --tls-cert/\n"
            << "                     --tls-key if given (needed for a real https gimkit page to\n"
            << "                     be able to fetch() it at all).\n"
            << "  --public-url       Origin embedded as \"serverUrl\" in matchmaker responses -\n"
            << "                     wherever this lobby server is actually reachable from the\n"
            << "                     browser, e.g. https://localhost:4444. Required with\n"
            << "                     --matchmaker-port; defaults to a best-effort localhost\n"
            << "                     guess otherwise.\n\n"
            << "Available gamemodes: ";
  for (const auto &name : openkit::gamemode_names()) {
    std::cerr << name << " ";
  }
  std::cerr << std::endl;
}

blueboat::Value load_questions_file(const std::string &path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("failed to open questions file: " + path);
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return openkit::expand_questions(blueboat::Value::parse(buffer.str()));
}

} // namespace

int main(int argc, char **argv) {
  openkit::tycoon::register_tycoon_gamemode();

  std::string gamemode_name = "tycoon";
  int port = 4000;
  std::string questions_path;
  std::string tls_cert_path;
  std::string tls_key_path;
  int matchmaker_port = 0;
  std::string public_url;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto next_value = [&]() -> std::string {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        std::exit(1);
      }
      return argv[++i];
    };

    if (arg == "--gamemode") {
      gamemode_name = next_value();
    } else if (arg == "--port") {
      port = std::stoi(next_value());
    } else if (arg == "--questions") {
      questions_path = next_value();
    } else if (arg == "--tls-cert") {
      tls_cert_path = next_value();
    } else if (arg == "--tls-key") {
      tls_key_path = next_value();
    } else if (arg == "--matchmaker-port") {
      matchmaker_port = std::stoi(next_value());
    } else if (arg == "--public-url") {
      public_url = next_value();
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << std::endl;
      print_usage(argv[0]);
      return 1;
    }
  }

  if (tls_cert_path.empty() != tls_key_path.empty()) {
    std::cerr << "--tls-cert and --tls-key must be given together" << std::endl;
    return 1;
  }

  const openkit::GamemodeInfo *gamemode = openkit::find_gamemode(gamemode_name);
  if (!gamemode) {
    std::cerr << "Unknown gamemode: " << gamemode_name << std::endl;
    print_usage(argv[0]);
    return 1;
  }

  if (questions_path.empty()) {
    std::cerr << "--questions is required" << std::endl;
    print_usage(argv[0]);
    return 1;
  }

  openkit::Catalog catalog = openkit::Catalog::load(gamemode->name);

  blueboat::Value default_questions;
  try {
    default_questions = load_questions_file(questions_path);
  } catch (const std::exception &e) {
    std::cerr << "Failed to load questions: " << e.what() << std::endl;
    return 1;
  }

  blueboat::ServerOptions options;
  options.storage = std::make_unique<blueboat::MemoryStorage>();
  options.pubsub = std::make_unique<blueboat::MemoryPubSub>();
  bool tls_enabled = !tls_cert_path.empty();
  if (tls_enabled) {
    options.tls = blueboat::TlsOptions{tls_cert_path, tls_key_path};
  }

  try {
    g_server = std::make_unique<blueboat::Server>(std::move(options));
  } catch (const std::exception &e) {
    std::cerr << "Failed to start server: " << e.what() << std::endl;
    return 1;
  }

  openkit::IntentRegistry intent_registry;

  g_server->register_room(gamemode->blueboat_room_type, [gamemode, catalog, default_questions, &intent_registry] {
    return gamemode->make_room(catalog, default_questions, blueboat::Value::object(), intent_registry);
  });

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  g_server->listen(port);
  std::cout << "server-1d listening on " << (tls_enabled ? "wss" : "ws") << "://<host>:" << port << "/blueboat/ (gamemode: " << gamemode_name << ")" << std::endl;

  std::unique_ptr<openkit::MatchmakerServer> matchmaker;
  if (matchmaker_port != 0) {
    if (public_url.empty()) {
      public_url = std::string(tls_enabled ? "https" : "http") + "://localhost:" + std::to_string(port);
      std::cerr << "Warning: --public-url not given, guessing " << public_url << " (won't work from another machine or a real gimkit page unless that's actually reachable)" << std::endl;
    }

    openkit::MatchmakerOptions matchmaker_options;
    matchmaker_options.public_url = public_url;
    matchmaker_options.default_questions = default_questions;
    if (tls_enabled) {
      matchmaker_options.tls = blueboat::TlsOptions{tls_cert_path, tls_key_path};
    }

    try {
      matchmaker = std::make_unique<openkit::MatchmakerServer>(*g_server, intent_registry, matchmaker_options);
      matchmaker->listen(matchmaker_port);
    } catch (const std::exception &e) {
      std::cerr << "Failed to start matchmaker: " << e.what() << std::endl;
      return 1;
    }
    std::cout << "matchmaker listening on " << (tls_enabled ? "https" : "http") << "://<host>:" << matchmaker_port << " (serverUrl: " << public_url << ")" << std::endl;
  }

  g_server->run_forever();
  return 0;
}
