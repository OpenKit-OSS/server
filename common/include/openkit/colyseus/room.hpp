#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "blueboat/common/json.hpp"
#include "blueboat/common/ws_connection.hpp"
#include "openkit/colyseus/schema.hpp"
#include "openkit/intent_registry.hpp"

namespace openkit::colyseus {

using Value = blueboat::Value;

class Client {
public:
  Client(std::string session_id, std::string id, blueboat::WsConnection *ws)
      : session_id_(std::move(session_id)), id_(std::move(id)), ws_(ws) {}

  const std::string &session_id() const { return session_id_; }
  const std::string &id() const { return id_; }

  void send(const std::string &type, const Value &payload = Value()) const;
  void send_raw(const std::string &frame) const;
  void close() const;

private:
  std::string session_id_;
  std::string id_;
  blueboat::WsConnection *ws_;
};

struct RoomInitOptions {
  std::string room_id;
  std::string room_type;
  std::function<void(const std::string &room_id)> on_room_disposed;
};

class Room {
public:
  virtual ~Room() = default;

  std::string room_id;
  std::string room_type;

  void initialize(RoomInitOptions init, const Value &create_options);

  virtual void on_create(const Value &options) {}
  virtual void on_join(Client &client, const Value &options) {}
  virtual void on_message(Client &client, const std::string &type,
                          const Value &data) {}
  virtual void on_leave(Client &client, bool intentional) {}
  virtual void on_dispose() {}

  void broadcast(const std::string &type, const Value &data = Value());
  std::size_t client_count() const;

  Client &add_client(const std::string &session_id, const std::string &id,
                     blueboat::WsConnection *ws, const Value &options);
  void remove_client(const std::string &session_id, bool intentional);
  void dispatch_message(const std::string &session_id, const std::string &type,
                        const Value &data);
  void dispose();

  const std::string &serializer_id() const { return serializer_id_; }
  const std::string &cached_reflection() const { return reflection_bytes_; }
  std::string schema_full_state();

protected:
  void init_schema_state(int root_class_id);
  schema::Node &state() { return *state_; }
  void broadcast_state_patch();

  std::recursive_mutex &mutex() { return mutex_; }

  Client *find_client_by_id(const std::string &id) const;

private:
  mutable std::recursive_mutex mutex_;
  std::vector<std::unique_ptr<Client>> clients_;
  std::function<void(const std::string &room_id)> on_room_disposed_;
  bool disposed_ = false;

  std::string serializer_id_ = "none";
  std::string reflection_bytes_;
  std::shared_ptr<schema::Node> state_;
  std::unique_ptr<schema::Encoder> encoder_;
};

using RoomFactory =
    std::function<std::unique_ptr<Room>(IntentRegistry &intent_registry)>;

} // namespace openkit::colyseus
