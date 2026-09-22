#include "openkit/tycoon_room.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>

#include "openkit/gamemode_registry.hpp"

namespace openkit::tycoon {

namespace {

double upgrade_value(const Value &upgrade_def, int level) {
  const Value &levels = upgrade_def.at("levels");
  int index = std::clamp(level - 1, 0, static_cast<int>(levels.size()) - 1);
  return levels.at(index).value("value", 1.0);
}

long long upgrade_price(const Value &upgrade_def, int next_level) {
  const Value &levels = upgrade_def.at("levels");
  int index =
      std::clamp(next_level - 1, 0, static_cast<int>(levels.size()) - 1);
  return levels.at(index).value("price", 0LL);
}

long long round_to_ll(double v) {
  return static_cast<long long>(std::llround(v));
}

long long round_to_nearest_5(double v) {
  return static_cast<long long>(std::llround(v / 5.0)) * 5;
}

const std::unordered_map<std::string, std::string> &upgrade_display_to_key() {
  static const std::unordered_map<std::string, std::string> map = {
      {"Money Per Question", "moneyPerQuestion"},
      {"Streak Bonus", "streakBonus"},
      {"Multiplier", "multiplier"},
      {"Insurance", "insurance"},
  };
  return map;
}

} // namespace

void TycoonRoom::on_create(const Value &) {
  Value merged_options = catalog_.game_options_defaults();
  if (creator_options.contains("gameOptions") &&
      creator_options.at("gameOptions").is_object()) {
    merged_options.merge_patch(creator_options.at("gameOptions"));
  }
  this->options = merged_options;

  if (creator_options.contains("questions") &&
      creator_options.at("questions").is_array()) {
    for (const auto &q : creator_options.at("questions")) {
      game_questions_.push_back(q);
    }
  } else {
    for (const auto &q : default_questions_) {
      game_questions_.push_back(q);
    }
  }
}

PlayerState *TycoonRoom::find_state(const std::string &session_id) {
  auto it = players_.find(session_id);
  return it == players_.end() ? nullptr : &it->second;
}

void TycoonRoom::send_static_state(blueboat::Client &client) {
  client.send("PLAYER_JOINS_STATIC_STATE",
              Value{
                  {"gameOptions", options},
                  {"powerups", catalog_.powerups()},
                  {"upgrades", catalog_.upgrades()},
                  {"themes", catalog_.themes()},
                  {"disabledThemes", catalog_.disabled_themes()},
                  {"news", catalog_.news()},
              });
}

void TycoonRoom::assign_next_question(blueboat::Client &client,
                                      PlayerState &state) {
  if (state.question_bank.empty()) {
    state.question_bank = game_questions_;
  }
  if (state.question_bank.empty()) {
    return;
  }

  bool needs_new_list =
      state.question_list.empty() ||
      state.question_index + 1 >= static_cast<int>(state.question_list.size());

  if (needs_new_list) {
    state.question_list.clear();
    for (const auto &q : state.question_bank) {
      state.question_list.push_back(q.value("_id", std::string()));
    }
    state.question_index = 0;
    client.send(
        "STATE_UPDATE",
        Value{{"type", "PLAYER_QUESTION_LIST"},
              {"value", Value{{"questionList", state.question_list},
                              {"questionIndex", state.question_index}}}});
  } else {
    state.question_index += 1;
    client.send("STATE_UPDATE", Value{{"type", "PLAYER_QUESTION_LIST_INDEX"},
                                      {"value", state.question_index}});
  }
}

void TycoonRoom::send_full_player_state(blueboat::Client &client,
                                        const PlayerState &state) {
  auto send_state = [&](const std::string &type, const Value &value) {
    client.send("STATE_UPDATE", Value{{"type", type}, {"value", value}});
  };

  Value question_values = Value::array();
  for (const auto &q : state.question_bank) {
    question_values.push_back(q);
  }
  send_state("GAME_QUESTIONS", question_values);
  send_state("PLAYER_QUESTION_LIST",
             Value{{"questionList", state.question_list},
                   {"questionIndex", state.question_index}});
  send_state("BALANCE", state.balance);
  send_state("USED_POWERUPS", state.used_powerups);
  send_state("PURCHASED_POWERUPS", state.purchased_powerups);
  send_state("PERSONAL_ACTIVE_POWERUPS", state.personal_active_powerups);
  send_balance_change(client, state);
  send_state("NAME", state.name);
  send_state("GROUP", Value{{"groupId", ""}, {"groupMemberId", ""}});
  send_state("THEME", state.theme);
  send_state("PURCHASED_THEMES", state.purchased_themes);

  Value upgrade_levels = Value::object();
  for (const auto &[k, v] : state.upgrade_levels) {
    upgrade_levels[k] = v;
  }
  send_state("UPGRADE_LEVELS", upgrade_levels);
  send_state("UPGRADE_PRICING_DISCOUNT", 1);
  send_state("GAME_STATUS", "gameplay");
  send_state("INCOME_MULTIPLIER", 1);
  send_state("LINK_INFO", Value{{"id", ""}, {"name", ""}});
  send_state("MAX_BALANCE", state.max_balance);
  send_state("DISABLED_POWERUPS", Value::array());
  send_state("FULL_SCREEN_PLAYER_BLACK", Value{{"on", false}});
  send_state(
      "SCREEN_ATTACK",
      Value{{"powerupName", ""}, {"attackerName", ""}, {"fullScreen", false}});
  send_state("STREAK_AMOUNT", state.streak);
  send_state("APPLIED_POWERUPS", Value::array());
  send_state("QUESTIONS_ANSWERED_CORRECTLY",
             state.questions_answered_correctly);
  send_state("QUESTIONS_ANSWERED_INCORRECTLY",
             state.questions_answered_incorrectly);
  send_state("BALANCE", state.balance);
  send_state("DISABLED_POWERUPS", Value::array());
  send_state("NAME", state.name);
}

void TycoonRoom::on_join(blueboat::Client &client, const Value &join_options) {
  PlayerState state;
  state.question_bank = game_questions_;

  auto [it, inserted] = players_.emplace(client.session_id, std::move(state));
  PlayerState &player = it->second;

  if (!player.question_bank.empty()) {
    for (const auto &q : player.question_bank) {
      player.question_list.push_back(q.value("_id", std::string()));
    }
  }

  send_static_state(client);
  send_full_player_state(client, player);

  if (join_options.contains("name") && join_options.at("name").is_string()) {
    player.name = join_options.at("name").get<std::string>();
    client.send("STATE_UPDATE",
                Value{{"type", "NAME"}, {"value", player.name}});
    client.send("STATE_UPDATE",
                Value{{"type", "GAME_STATUS"}, {"value", "gameplay"}});
    client.send("STATE_UPDATE", Value{{"type", "DISABLED_POWERUPS"},
                                      {"value", Value::array()}});
  }

  broadcast_leaderboard();
}

void TycoonRoom::on_leave(blueboat::Client &client, bool) {
  players_.erase(client.session_id);
}

Value TycoonRoom::compute_balance_change(const PlayerState &state) const {
  double money_per_question = upgrade_value(
      *catalog_.find_by_name(catalog_.upgrades(), "Money Per Question"),
      state.upgrade_levels.at("moneyPerQuestion"));
  double streak_bonus =
      upgrade_value(*catalog_.find_by_name(catalog_.upgrades(), "Streak Bonus"),
                    state.upgrade_levels.at("streakBonus"));
  double multiplier =
      upgrade_value(*catalog_.find_by_name(catalog_.upgrades(), "Multiplier"),
                    state.upgrade_levels.at("multiplier"));
  double insurance =
      upgrade_value(*catalog_.find_by_name(catalog_.upgrades(), "Insurance"),
                    state.upgrade_levels.at("insurance"));

  long long if_correct = round_to_ll(
      (money_per_question + state.streak * streak_bonus) * multiplier);
  long long if_incorrect = -round_to_ll(money_per_question * insurance);

  return Value{{"balanceChangeIfCorrect", if_correct},
               {"balanceChangeIfIncorrect", if_incorrect}};
}

void TycoonRoom::send_balance_change(blueboat::Client &client,
                                     const PlayerState &state) {
  client.send("STATE_UPDATE", Value{{"type", "BALANCE_CHANGE"},
                                    {"value", compute_balance_change(state)}});
}

void TycoonRoom::on_message(blueboat::Client &client, const std::string &key,
                            const Value &data) {
  if (key == "QUESTION_ANSWERED") {
    handle_question_answered(client, data);
  } else if (key == "UPGRADE_PURCHASED") {
    handle_upgrade_purchased(client, data);
  } else if (key == "POWERUP_PURCHASED") {
    handle_powerup_purchased(client, data);
  } else if (key == "POWERUP_ACTIVATED") {
    handle_powerup_activated(client, data);
  } else if (key == "POWERUP_ATTACK") {
    handle_powerup_attack(client, data);
  } else if (key == "THEME_PURCHASED") {
    handle_theme_purchased(client, data);
  } else if (key == "THEME_APPLIED") {
    handle_theme_applied(client, data);
  } else if (key == "PLAYER_LEADERBOARD_REQUESTED") {
    broadcast_leaderboard();
  }
}

void TycoonRoom::handle_question_answered(blueboat::Client &client,
                                          const Value &data) {
  PlayerState *state = find_state(client.session_id);
  if (!state) {
    return;
  }
  if (is_frozen(*state)) {
    return;
  }

  std::string question_id = data.value("questionId", std::string());
  std::string answer_id = data.value("answer", std::string());

  auto question_it =
      std::find_if(state->question_bank.begin(), state->question_bank.end(),
                   [&](const Value &q) {
                     return q.value("_id", std::string()) == question_id;
                   });
  if (question_it == state->question_bank.end()) {
    return;
  }

  bool correct = false;
  for (const auto &answer : question_it->value("answers", Value::array())) {
    if (answer.value("_id", std::string()) == answer_id &&
        answer.value("correct", false)) {
      correct = true;
      break;
    }
  }

  Value balance_change = compute_balance_change(*state);

  if (correct) {
    state->balance +=
        balance_change.at("balanceChangeIfCorrect").get<long long>();
    state->streak += 1;
    state->questions_answered_correctly += 1;
    client.send("STATE_UPDATE",
                Value{{"type", "QUESTIONS_ANSWERED_CORRECTLY"},
                      {"value", state->questions_answered_correctly}});
  } else {
    state->balance +=
        balance_change.at("balanceChangeIfIncorrect").get<long long>();
    state->streak = 0;
    state->questions_answered_incorrectly += 1;
    client.send("STATE_UPDATE",
                Value{{"type", "QUESTIONS_ANSWERED_INCORRECTLY"},
                      {"value", state->questions_answered_incorrectly}});
  }

  state->max_balance = std::max(state->max_balance, state->balance);

  client.send("STATE_UPDATE",
              Value{{"type", "BALANCE"}, {"value", state->balance}});
  assign_next_question(client, *state);
  client.send("STATE_UPDATE",
              Value{{"type", "STREAK_AMOUNT"}, {"value", state->streak}});
  send_balance_change(client, *state);

  broadcast_leaderboard();
}

void TycoonRoom::handle_upgrade_purchased(blueboat::Client &client,
                                          const Value &data) {
  PlayerState *state = find_state(client.session_id);
  if (!state) {
    return;
  }
  if (is_frozen(*state)) {
    return;
  }

  std::string upgrade_name = data.value("upgradeName", std::string());
  int next_level = data.value("level", 0);

  const auto &display_to_key = upgrade_display_to_key();
  auto key_it = display_to_key.find(upgrade_name);
  if (key_it == display_to_key.end()) {
    return;
  }

  auto upgrade_def = catalog_.find_by_name(catalog_.upgrades(), upgrade_name);
  if (!upgrade_def) {
    return;
  }

  int current_level = state->upgrade_levels[key_it->second];
  if (next_level != current_level + 1) {
    return;
  }

  long long price = upgrade_price(*upgrade_def, next_level);
  if (state->balance < price) {
    return;
  }

  state->balance -= price;
  state->upgrade_levels[key_it->second] = next_level;
  state->streak = 0;

  client.send("STATE_UPDATE",
              Value{{"type", "STREAK_AMOUNT"}, {"value", state->streak}});
  client.send("STATE_UPDATE",
              Value{{"type", "BALANCE"}, {"value", state->balance}});

  send_upgrade_levels(client, *state);
  send_balance_change(client, *state);
  send_activity_to_host(state->name, "upgraded " + upgrade_name + " to level " +
                                         std::to_string(next_level));

  broadcast_leaderboard();
}

void TycoonRoom::send_upgrade_levels(blueboat::Client &client,
                                     const PlayerState &state) {
  Value upgrade_levels = Value::object();
  for (const auto &[k, v] : state.upgrade_levels) {
    upgrade_levels[k] = v;
  }
  client.send("STATE_UPDATE",
              Value{{"type", "UPGRADE_LEVELS"}, {"value", upgrade_levels}});
}

void TycoonRoom::handle_powerup_purchased(blueboat::Client &client,
                                          const Value &data) {
  PlayerState *state = find_state(client.session_id);
  if (!state) {
    return;
  }
  if (is_frozen(*state)) {
    return;
  }

  std::string powerup_name =
      data.is_string() ? data.get<std::string>() : std::string();
  auto powerup_def = catalog_.find_by_name(catalog_.powerups(), powerup_name);
  if (!powerup_def) {
    return;
  }

  double base_cost = powerup_def->value("baseCost", 0.0);
  double percentage_cost = powerup_def->value("percentageCost", 0.0);
  long long price = round_to_nearest_5(
      base_cost + percentage_cost * static_cast<double>(state->balance));
  if (state->balance < price) {
    return;
  }

  state->balance -= price;
  state->purchased_powerups.push_back(powerup_name);

  client.send("STATE_UPDATE", Value{{"type", "PURCHASED_POWERUPS"},
                                    {"value", state->purchased_powerups}});
  client.send("STATE_UPDATE",
              Value{{"type", "BALANCE"}, {"value", state->balance}});

  broadcast_leaderboard();
}

namespace {
bool is_targeted_powerup(const std::string &name) {
  static const std::unordered_map<std::string, bool> targeted = {
      {"Icer", true},       {"Blurred Screen", true}, {"outnumbered", true},
      {"Subtractor", true}, {"Giving", true},
  };
  auto it = targeted.find(name);
  return it != targeted.end() && it->second;
}
} // namespace

void TycoonRoom::handle_powerup_activated(blueboat::Client &client,
                                          const Value &data) {
  PlayerState *state = find_state(client.session_id);
  if (!state) {
    return;
  }

  std::string powerup_name =
      data.is_string() ? data.get<std::string>() : std::string();
  if (is_targeted_powerup(powerup_name)) {
    return;
  }

  auto purchased_it = std::find(state->purchased_powerups.begin(),
                                state->purchased_powerups.end(), powerup_name);
  if (purchased_it == state->purchased_powerups.end()) {
    return;
  }

  state->purchased_powerups.erase(purchased_it);
  state->used_powerups.push_back(powerup_name);

  client.send("STATE_UPDATE", Value{{"type", "PURCHASED_POWERUPS"},
                                    {"value", state->purchased_powerups}});
  client.send("STATE_UPDATE", Value{{"type", "USED_POWERUPS"},
                                    {"value", state->used_powerups}});

  if (powerup_name == "Quadgrader") {
    for (const auto &[display_name, key] : upgrade_display_to_key()) {
      auto upgrade_def =
          catalog_.find_by_name(catalog_.upgrades(), display_name);
      if (!upgrade_def) {
        continue;
      }
      int max_level = static_cast<int>(upgrade_def->at("levels").size());
      int &level = state->upgrade_levels[key];
      level = std::min(level + 1, max_level);
    }
    send_upgrade_levels(client, *state);
    send_balance_change(client, *state);
    return;
  }

  // TODO: Rebooter, Minute To Win It, Discounter, Mini/Mega Bonus
}

void TycoonRoom::handle_powerup_attack(blueboat::Client &client,
                                       const Value &data) {
  PlayerState *attacker_state = find_state(client.session_id);
  if (!attacker_state) {
    return;
  }

  std::string powerup_name = data.value("name", std::string());
  std::string target_id = data.value("target", std::string());

  auto purchased_it =
      std::find(attacker_state->purchased_powerups.begin(),
                attacker_state->purchased_powerups.end(), powerup_name);
  if (purchased_it == attacker_state->purchased_powerups.end()) {
    return;
  }

  blueboat::Client *target_client = nullptr;
  for (const auto &c : clients()) {
    if (c->id == target_id) {
      target_client = c.get();
      break;
    }
  }
  if (!target_client) {
    return;
  }
  PlayerState *target_state = find_state(target_client->session_id);
  if (!target_state) {
    return;
  }

  attacker_state->purchased_powerups.erase(purchased_it);
  attacker_state->used_powerups.push_back(powerup_name);
  client.send("STATE_UPDATE",
              Value{{"type", "PURCHASED_POWERUPS"},
                    {"value", attacker_state->purchased_powerups}});
  client.send("STATE_UPDATE", Value{{"type", "USED_POWERUPS"},
                                    {"value", attacker_state->used_powerups}});

  bool full_screen = false;
  std::string verb = "Attacked";
  if (powerup_name == "Icer") {
    target_state->frozen_until =
        std::chrono::steady_clock::now() + std::chrono::seconds(15);
    full_screen = true;
    verb = "Froze";
  }

  target_client->send(
      "STATE_UPDATE",
      Value{{"type", "SCREEN_ATTACK"},
            {"value", Value{{"attackerName", attacker_state->name},
                            {"powerupName", powerup_name},
                            {"fullScreen", full_screen}}}});
  client.send("TOAST", Value{{"message", verb + " " + target_state->name + "!"},
                             {"type", "success"}});

  std::string lower_verb = verb;
  std::transform(lower_verb.begin(), lower_verb.end(), lower_verb.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  send_activity_to_host(attacker_state->name,
                        lower_verb + " " + target_state->name + "!",
                        powerup_name == "Icer" ? "red" : "");

  broadcast_leaderboard();
}

bool TycoonRoom::is_frozen(const PlayerState &state) const {
  return state.frozen_until &&
         std::chrono::steady_clock::now() < *state.frozen_until;
}

void TycoonRoom::send_activity_to_host(const std::string &name,
                                       const std::string &action,
                                       const std::string &color) {
  if (owner.id.empty()) {
    return;
  }
  for (const auto &c : clients()) {
    if (c->id == owner.id) {
      Value item{{"name", name}, {"action", action}};
      if (!color.empty()) {
        item["customTextColor"] = color;
      }
      c->send("NEW_ACTIVITY_ITEM", item);
      return;
    }
  }
}

void TycoonRoom::handle_theme_purchased(blueboat::Client &client,
                                        const Value &data) {
  PlayerState *state = find_state(client.session_id);
  if (!state) {
    return;
  }
  if (is_frozen(*state)) {
    return;
  }

  std::string theme_name =
      data.is_string() ? data.get<std::string>() : std::string();
  auto theme_def = catalog_.find_by_name(catalog_.themes(), theme_name);
  if (!theme_def) {
    return;
  }
  if (std::find(state->purchased_themes.begin(), state->purchased_themes.end(),
                theme_name) != state->purchased_themes.end()) {
    return;
  }

  long long price = theme_def->value("cost", 0LL);
  if (state->balance < price) {
    return;
  }

  state->balance -= price;
  state->purchased_themes.push_back(theme_name);

  client.send("STATE_UPDATE",
              Value{{"type", "BALANCE"}, {"value", state->balance}});
  client.send("STATE_UPDATE", Value{{"type", "PURCHASED_THEMES"},
                                    {"value", state->purchased_themes}});
}

void TycoonRoom::handle_theme_applied(blueboat::Client &client,
                                      const Value &data) {
  PlayerState *state = find_state(client.session_id);
  if (!state) {
    return;
  }
  if (is_frozen(*state)) {
    return;
  }

  std::string theme_name =
      data.is_string() ? data.get<std::string>() : std::string();
  if (std::find(state->purchased_themes.begin(), state->purchased_themes.end(),
                theme_name) == state->purchased_themes.end()) {
    return;
  }

  state->theme = theme_name;
  client.send("STATE_UPDATE",
              Value{{"type", "THEME"}, {"value", state->theme}});
}

void TycoonRoom::broadcast_leaderboard() {
  Value items = Value::array();
  for (const auto &client_ptr : clients()) {
    const PlayerState *state = find_state(client_ptr->session_id);
    if (!state) {
      continue;
    }
    items.push_back(Value{
        {"id", client_ptr->id},
        {"name", state->name},
        {"theme", state->theme},
        {"activePowerups", state->personal_active_powerups},
        {"balance", state->balance},
    });
  }
  broadcast("UPDATED_PLAYER_LEADERBOARD", Value{{"items", items}, {"key", ""}});
}

void register_tycoon_gamemode() {
  register_gamemode(GamemodeInfo{
      "tycoon",
      "Tycoon",
      [](Catalog catalog,
         Value default_questions) -> std::unique_ptr<blueboat::Room> {
        return std::make_unique<TycoonRoom>(std::move(catalog),
                                            std::move(default_questions));
      },
  });
}

} // namespace openkit::tycoon
