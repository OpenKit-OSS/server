#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "blueboat/room.hpp"
#include "openkit/catalog.hpp"

namespace openkit::tycoon {

using blueboat::Value;

struct PlayerState {
  std::string name = "Player [Still Entering Name]";
  std::string theme = "Default";
  std::vector<std::string> purchased_themes{"Default"};

  std::vector<std::string> purchased_powerups;
  std::vector<std::string> used_powerups;
  std::vector<Value> personal_active_powerups;

  std::unordered_map<std::string, int> upgrade_levels{
      {"moneyPerQuestion", 1},
      {"streakBonus", 1},
      {"multiplier", 1},
      {"insurance", 1},
  };

  long long balance = 0;
  long long max_balance = 0;
  int streak = 0;
  int questions_answered_correctly = 0;
  int questions_answered_incorrectly = 0;

  std::vector<Value> question_bank;
  std::vector<std::string> question_list;
  int question_index = 0;

  std::optional<std::chrono::steady_clock::time_point> frozen_until;

  double next_question_multiplier = 1.0;

  std::optional<std::chrono::steady_clock::time_point> income_boost_until;
  double income_boost_factor = 1.0;

  std::optional<std::chrono::steady_clock::time_point> discount_until;
  double discount_factor = 0.0;
};

class TycoonRoom : public blueboat::Room {
public:
  TycoonRoom(Catalog catalog, Value default_questions = Value::array())
      : catalog_(std::move(catalog)),
        default_questions_(std::move(default_questions)) {}

  void on_create(const Value &options) override;
  void on_join(blueboat::Client &client, const Value &options) override;
  void on_message(blueboat::Client &client, const std::string &key,
                  const Value &data) override;
  void on_leave(blueboat::Client &client, bool intentional) override;

private:
  void send_static_state(blueboat::Client &client);
  void send_full_player_state(blueboat::Client &client,
                              const PlayerState &state);
  void broadcast_leaderboard();

  void handle_question_answered(blueboat::Client &client, const Value &data);
  void handle_upgrade_purchased(blueboat::Client &client, const Value &data);
  void handle_powerup_purchased(blueboat::Client &client, const Value &data);
  void handle_powerup_activated(blueboat::Client &client, const Value &data);
  void handle_powerup_attack(blueboat::Client &client, const Value &data);
  void handle_theme_purchased(blueboat::Client &client, const Value &data);
  void handle_theme_applied(blueboat::Client &client, const Value &data);

  bool is_frozen(const PlayerState &state) const;
  void send_activity_to_host(const std::string &name, const std::string &action,
                             const std::string &color = "");

  Value compute_balance_change(const PlayerState &state) const;
  void send_balance_change(blueboat::Client &client, const PlayerState &state);
  void send_upgrade_levels(blueboat::Client &client, const PlayerState &state);
  void assign_next_question(blueboat::Client &client, PlayerState &state);

  PlayerState *find_state(const std::string &session_id);

  Catalog catalog_;
  Value default_questions_;
  std::unordered_map<std::string, PlayerState> players_;
  std::vector<Value> game_questions_;
};

void register_tycoon_gamemode();

} // namespace openkit::tycoon
