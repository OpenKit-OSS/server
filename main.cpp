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
               "       [--auto-create-room] [--room-id <id>] [--tls-cert <file> --tls-key <file>]\n\n"
            << "  --questions        Path to a JSON question file in the simple format:\n"
            << "                       [{ \"text\": \"2+2?\", \"answers\": [\"4\", \"5\"], \"correct\": [0] }]\n"
            << "                     \"correct\" may be a single index or a list of indices, and\n"
            << "                     defaults to [0].\n"
            << "  --gamemode         Which gamemode to host (default: tycoon)\n"
            << "  --port             TCP port to listen on (default: 4000)\n"
            << "  --auto-create-room Create one room at startup (id is printed if not given via\n"
            << "                     --room-id) instead of waiting for a client to create one -\n"
            << "                     useful when a matchmaker in front of this server always\n"
            << "                     expects a specific room to already exist.\n"
            << "  --room-id          Fixed room ID to use with --auto-create-room; implies it.\n"
            << "  --tls-cert/--tls-key\n"
            << "                     PEM cert/key to terminate TLS (wss://) directly, e.g. from\n"
            << "                     mkcert. Required together; plain ws:// is used otherwise.\n\n"
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
  bool auto_create_room = false;
  std::string room_id;
  std::string tls_cert_path;
  std::string tls_key_path;

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
    } else if (arg == "--auto-create-room") {
      auto_create_room = true;
    } else if (arg == "--room-id") {
      room_id = next_value();
      auto_create_room = true;
    } else if (arg == "--tls-cert") {
      tls_cert_path = next_value();
    } else if (arg == "--tls-key") {
      tls_key_path = next_value();
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

  g_server->register_room(gamemode->blueboat_room_type, [gamemode, catalog, default_questions] { return gamemode->make_room(catalog, default_questions); });

  if (auto_create_room) {
    try {
      std::string created_id = g_server->create_room(gamemode->blueboat_room_type, room_id);
      std::cout << "Auto-created room " << created_id << " (type: " << gamemode->blueboat_room_type << ")" << std::endl;
    } catch (const std::exception &e) {
      std::cerr << "Failed to auto-create room: " << e.what() << std::endl;
      return 1;
    }
  }

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  g_server->listen(port);
  std::cout << "server-1d listening on " << (tls_enabled ? "wss" : "ws") << "://<host>:" << port << "/blueboat/ (gamemode: " << gamemode_name << ")" << std::endl;

  g_server->run_forever();
  return 0;
}
