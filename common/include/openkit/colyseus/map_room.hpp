#pragma once

#include <string>
#include <unordered_map>

#include "blueboat/common/scheduler.hpp"
#include "openkit/colyseus/room.hpp"
#include "openkit/intent_registry.hpp"
#include "openkit/map_catalog.hpp"

namespace openkit::colyseus {

class MapRoom : public Room {
public:
  MapRoom(IntentRegistry &intent_registry, MapCatalog map_catalog)
      : intent_registry_(intent_registry),
        map_catalog_(std::move(map_catalog)) {}

  void on_create(const Value &options) override;
  void on_join(Client &client, const Value &options) override;
  void on_message(Client &client, const std::string &type,
                  const Value &data) override;
  void on_leave(Client &client, bool intentional) override;
  void on_dispose() override;

private:
  void handle_request_initial_world(Client &client);
  void handle_input(Client &client, const Value &data);
  void handle_start_game(Client &client, const Value &data);
  void handle_set_active_interactive_item(Client &client, const Value &data);
  void handle_aiming(Client &client, const Value &data);
  void handle_add_game_time(Client &client, const Value &data);
  void handle_end_game(Client &client, const Value &data);
  void handle_kick_player(Client &client, const Value &data);
  void assign_teams(const std::string &owner_id, bool owner_as_spectator,
                    const Value &custom_teams);
  void apply_spawn_positions();
  void apply_game_start_devices(double countdown_end);
  void grant_starting_inventory(std::shared_ptr<schema::Node> character);

  void schedule_tick();
  blueboat::TimerHandle tick_timer_;

  IntentRegistry &intent_registry_;
  MapCatalog map_catalog_;
  Value game_options_ = Value::object();
  Value game_settings_ = Value::object();
  double countdown_end_ = 0;
};

} // namespace openkit::colyseus
