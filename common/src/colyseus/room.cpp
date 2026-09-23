#include "openkit/colyseus/room.hpp"

#include <algorithm>

#include "openkit/colyseus/protocol.hpp"

namespace openkit::colyseus {

void Client::send(const std::string &type, const Value &payload) const {
  if (!ws_)
    return;
  ws_->send_binary(build_room_data_frame(type, payload));
}

void Client::send_raw(const std::string &frame) const {
  if (!ws_)
    return;
  ws_->send_binary(frame);
}

void Client::close() const {
  if (!ws_)
    return;
  ws_->send_binary(build_leave_room_frame());
  ws_->close();
}

void Room::initialize(RoomInitOptions init, const Value &create_options) {
  room_id = init.room_id;
  room_type = init.room_type;
  on_room_disposed_ = std::move(init.on_room_disposed);
  on_create(create_options);
}

void Room::broadcast(const std::string &type, const Value &data) {
  std::lock_guard<std::recursive_mutex> guard(mutex_);
  for (auto &client : clients_) {
    client->send(type, data);
  }
}

std::size_t Room::client_count() const {
  std::lock_guard<std::recursive_mutex> guard(mutex_);
  return clients_.size();
}

Client &Room::add_client(const std::string &session_id, const std::string &id,
                         blueboat::WsConnection *ws, const Value &options) {
  std::lock_guard<std::recursive_mutex> guard(mutex_);

  auto client = std::make_unique<Client>(session_id, id, ws);
  Client *raw = client.get();

  on_join(*raw, options);
  if (state_) {
    raw->send_raw(build_room_state_frame(schema_full_state()));
  }

  clients_.push_back(std::move(client));
  return *raw;
}

void Room::remove_client(const std::string &session_id, bool intentional) {
  std::lock_guard<std::recursive_mutex> guard(mutex_);

  auto it = std::find_if(clients_.begin(), clients_.end(),
                         [&](const std::unique_ptr<Client> &c) {
                           return c->session_id() == session_id;
                         });
  if (it == clients_.end()) {
    return;
  }
  std::unique_ptr<Client> removed = std::move(*it);
  clients_.erase(it);
  bool now_empty = clients_.empty();

  on_leave(*removed, intentional);
  if (now_empty) {
    dispose();
  }
}

void Room::dispatch_message(const std::string &session_id,
                            const std::string &type, const Value &data) {
  std::lock_guard<std::recursive_mutex> guard(mutex_);

  auto it = std::find_if(clients_.begin(), clients_.end(),
                         [&](const std::unique_ptr<Client> &c) {
                           return c->session_id() == session_id;
                         });
  if (it == clients_.end()) {
    return;
  }
  on_message(**it, type, data);
}

void Room::init_schema_state(int root_class_id) {
  state_ = std::make_shared<schema::Node>(root_class_id);
  encoder_ = std::make_unique<schema::Encoder>(state_);
  serializer_id_ = "schema";
  reflection_bytes_ = schema::Encoder::encode_reflection(root_class_id);
}

std::string Room::schema_full_state() {
  return encoder_ ? encoder_->encode_all() : std::string();
}

void Room::broadcast_state_patch() {
  std::lock_guard<std::recursive_mutex> guard(mutex_);
  if (!encoder_)
    return;
  std::string patch = encoder_->encode_patch();
  if (patch.empty())
    return;
  std::string frame = build_room_state_patch_frame(patch);
  for (auto &client : clients_) {
    client->send_raw(frame);
  }
}

void Room::dispose() {
  if (disposed_)
    return;
  disposed_ = true;
  on_dispose();
  if (on_room_disposed_) {
    on_room_disposed_(room_id);
  }
}

} // namespace openkit::colyseus
