#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "blueboat/common/http.hpp"
#include "blueboat/common/json.hpp"
#include "blueboat/common/socket.hpp"
#include "openkit/colyseus/room.hpp"
#include "openkit/experience_registry.hpp"
#include "openkit/intent_registry.hpp"
#include "openkit/map_catalog.hpp"

namespace openkit::colyseus {

using Value = blueboat::Value;

class RoomHost {
public:
  RoomHost(IntentRegistry &intent_registry,
           ExperienceRegistry &experience_registry, std::string process_id,
           std::string default_map_id = "");

  bool is_create_request(const blueboat::http::Request &request) const;
  bool is_join_by_id_request(const blueboat::http::Request &request) const;
  bool is_room_websocket_request(const blueboat::http::Request &request) const;

  void handle_create(blueboat::Socket &sock,
                     const blueboat::http::Request &request);
  void handle_join_by_id(blueboat::Socket &sock,
                         const blueboat::http::Request &request);

  void handle_websocket(std::unique_ptr<blueboat::Socket> sock,
                        const blueboat::http::Request &request);

  bool has_room(const std::string &room_id) const;
  const std::string &process_id() const { return process_id_; }

private:
  struct PendingReservation {
    std::string room_id;
    Value options;
    std::string client_id;
  };

  std::optional<std::string> room_id_for_path(const std::string &path) const;
  std::optional<std::string>
  resolve_map_id(const std::string &experience_id) const;
  const MapCatalog &catalog_for_map(const std::string &map_id);

  IntentRegistry &intent_registry_;
  ExperienceRegistry &experience_registry_;
  std::string process_id_;
  std::string default_map_id_;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::unique_ptr<Room>> rooms_;
  std::unordered_map<std::string, PendingReservation> pending_sessions_;
  std::unordered_map<std::string, MapCatalog> map_catalog_cache_;
};

} // namespace openkit::colyseus
