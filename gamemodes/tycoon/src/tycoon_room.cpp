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

Value fixed_game_options() {
  return Value{{"type", "live"},
               {"specialGameType", Value::array({"CLASSIC"})},
               {"handicap", -50}};
}

struct TycoonVariant {
  Value special_game_type = Value::array({"CLASSIC"});
  double income_multiplier = 1.0;
  double upgrade_pricing_discount = 1.0;
};

const std::unordered_map<std::string, TycoonVariant> &known_tycoon_variants() {
  static const std::unordered_map<std::string, TycoonVariant> variants = {
      {"618737355757c900231ff2d2",
       TycoonVariant{Value::array({"RICH"}), 500.0, 500.0}},
  };
  return variants;
}

Value resolve_tycoon_intent_extras(const Value &create_body) {
  std::string experience_id = create_body.value("experienceId", std::string());
  const auto &variants = known_tycoon_variants();
  auto it = variants.find(experience_id);
  const TycoonVariant &variant =
      it != variants.end() ? it->second : TycoonVariant{};
  return Value{
      {"specialGameType", variant.special_game_type},
      {"incomeMultiplier", variant.income_multiplier},
      {"upgradePricingDiscount", variant.upgrade_pricing_discount},
  };
}

} // namespace

void TycoonRoom::on_create(const Value &) {
  std::string intent_id = creator_options.value("intentId", std::string());
  std::optional<Value> intent_data =
      intent_id.empty() ? std::nullopt
                        : intent_registry_.resolve_intent(intent_id);

  if (intent_data) {
    Value extras = intent_data->value("extras", Value::object());

    Value merged_options = fixed_game_options();
    if (extras.contains("specialGameType")) {
      merged_options["specialGameType"] = extras.at("specialGameType");
    }
    merged_options.merge_patch(
        intent_data->value("gameOptions", Value::object()));
    this->options = merged_options;

    income_multiplier_ = extras.value("incomeMultiplier", 1.0);
    upgrade_pricing_discount_ = extras.value("upgradePricingDiscount", 1.0);

    for (const auto &q : intent_data->value("questions", Value::array())) {
      game_questions_.push_back(q);
    }
    game_code_ = intent_data->value("gameCode", std::string());
    game_status_ = "join";
    intent_registry_.link_code_to_room(game_code_, room_id);
    return;
  }

  Value merged_options = fixed_game_options();
  merged_options.merge_patch((creator_options.contains("gameOptions") &&
                              creator_options.at("gameOptions").is_object())
                                 ? creator_options.at("gameOptions")
                                 : default_game_options_);
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

  if (creator_options.contains("gameCode")) {
    game_code_ = creator_options.value("gameCode", std::string());
    game_status_ = "join";
  }
}

PlayerState *TycoonRoom::find_state(const std::string &client_id) {
  auto it = players_.find(client_id);
  return it == players_.end() ? nullptr : &it->second;
}

Value TycoonRoom::filtered_powerups() const {
  bool music_on = options.value("music", false);
  bool clean_only = options.value("cleanPowerupsOnly", false);

  Value result = Value::array();
  for (const auto &powerup : catalog_.powerups()) {
    bool hide_for_music_off = false;
    bool hide_for_clean_only = false;
    for (const auto &tag : powerup.value("disabled", Value::array())) {
      if (tag == "musicOff") {
        hide_for_music_off = true;
      } else if (tag == "cleanOnly") {
        hide_for_clean_only = true;
      }
    }
    if (hide_for_music_off && !music_on) {
      continue;
    }
    if (hide_for_clean_only && clean_only) {
      continue;
    }
    result.push_back(powerup);
  }
  return result;
}

void TycoonRoom::send_static_state(blueboat::Client &client) {
  client.send("PLAYER_JOINS_STATIC_STATE",
              Value{
                  {"gameOptions", options},
                  {"powerups", filtered_powerups()},
                  {"upgrades", catalog_.upgrades()},
                  {"themes", catalog_.themes()},
                  {"disabledThemes", catalog_.disabled_themes()},
                  {"news", catalog_.news()},
              });
}

void TycoonRoom::send_host_static_state(blueboat::Client &client) {
  client.send("HOST_STATIC_STATE", Value{
                                       {"options", options},
                                       {"powerups", filtered_powerups()},
                                       {"themes", catalog_.themes()},
                                       {"gameCode", ""},
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
  send_state("UPGRADE_PRICING_DISCOUNT", upgrade_pricing_discount_);
  send_state("GAME_STATUS", game_status_);
  send_state("INCOME_MULTIPLIER", income_multiplier_);
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
  if (!game_code_.empty() && owner.id.empty()) {
    owner.id = client.id;
    owner.session_id = client.session_id;
  }

  bool is_host = !owner.id.empty() && client.id == owner.id;
  if (is_host) {
    send_host_static_state(client);
    return;
  }

  auto [it, inserted] = players_.try_emplace(client.id);
  PlayerState &player = it->second;

  if (inserted) {
    player.question_bank = game_questions_;
    for (const auto &q : player.question_bank) {
      player.question_list.push_back(q.value("_id", std::string()));
    }
  }

  send_static_state(client);
  send_full_player_state(client, player);

  std::string resolved_name;
  if (join_options.contains("name") && join_options.at("name").is_string()) {
    resolved_name = join_options.at("name").get<std::string>();
  } else if (join_options.contains("intent") &&
             join_options.at("intent").is_string()) {
    auto intent_data = intent_registry_.resolve_intent(
        join_options.at("intent").get<std::string>());
    if (intent_data) {
      resolved_name = intent_data->value("name", std::string());
    }
  }

  if (!resolved_name.empty()) {
    player.name = resolved_name;
    client.send("STATE_UPDATE",
                Value{{"type", "NAME"}, {"value", player.name}});
    client.send("STATE_UPDATE",
                Value{{"type", "GAME_STATUS"}, {"value", game_status_}});
    client.send("STATE_UPDATE", Value{{"type", "DISABLED_POWERUPS"},
                                      {"value", Value::array()}});
  }

  broadcast_leaderboard();
}

void TycoonRoom::on_leave(blueboat::Client &client, bool) {
  allow_reconnection(client, 30);
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

  double effective_multiplier = multiplier * state.next_question_multiplier;
  auto now = std::chrono::steady_clock::now();
  if (state.income_boost_until && now < *state.income_boost_until) {
    effective_multiplier *= state.income_boost_factor;
  }

  long long if_correct =
      round_to_ll((money_per_question + state.streak * streak_bonus) *
                  effective_multiplier * income_multiplier_);
  long long if_incorrect =
      -round_to_ll(money_per_question * insurance * income_multiplier_);

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
  } else if (key == "NEW_GAME_STATUS") {
    handle_new_game_status(client, data);
  }
}

void TycoonRoom::handle_new_game_status(blueboat::Client &client,
                                        const Value &data) {
  if (owner.id.empty() || client.id != owner.id) {
    return;
  }
  std::string status =
      data.is_string() ? data.get<std::string>() : std::string();
  if (status.empty()) {
    return;
  }
  game_status_ = status;

  if (status == "join") {
    client.send("VIEWABLE_GAME_CODE", game_code_);
  }
  broadcast("STATE_UPDATE",
            Value{{"type", "GAME_STATUS"}, {"value", game_status_}});
}

void TycoonRoom::handle_question_answered(blueboat::Client &client,
                                          const Value &data) {
  PlayerState *state = find_state(client.id);
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
  state->next_question_multiplier = 1.0;

  client.send("STATE_UPDATE",
              Value{{"type", "BALANCE"}, {"value", state->balance}});
  assign_next_question(client, *state);
  client.send("STATE_UPDATE",
              Value{{"type", "STREAK_AMOUNT"}, {"value", state->streak}});
  client.send("STATE_UPDATE",
              Value{{"type", "BALANCE_CHANGE"}, {"value", balance_change}});

  broadcast_leaderboard();
}

void TycoonRoom::handle_upgrade_purchased(blueboat::Client &client,
                                          const Value &data) {
  PlayerState *state = find_state(client.id);
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
  if (next_level <= current_level) {
    return;
  }

  long long price = 0;
  for (int level = current_level + 1; level <= next_level; ++level) {
    price += upgrade_price(*upgrade_def, level);
  }
  price = round_to_ll(static_cast<double>(price) * upgrade_pricing_discount_);
  if (state->discount_until &&
      std::chrono::steady_clock::now() < *state->discount_until) {
    price = round_to_ll(static_cast<double>(price) *
                        (1.0 - state->discount_factor));
  }
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
  PlayerState *state = find_state(client.id);
  if (!state) {
    return;
  }
  if (is_frozen(*state)) {
    return;
  }

  std::string powerup_name =
      data.is_string() ? data.get<std::string>() : std::string();
  auto powerup_def = catalog_.find_by_name(filtered_powerups(), powerup_name);
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
  PlayerState *state = find_state(client.id);
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

  auto now = std::chrono::steady_clock::now();

  if (powerup_name == "Mini Bonus") {
    state->next_question_multiplier = 2.0;
    send_balance_change(client, *state);
  } else if (powerup_name == "Mega Bonus") {
    state->next_question_multiplier = 5.0;
    send_balance_change(client, *state);
  } else if (powerup_name == "minuteMoreEarnings") {
    state->income_boost_until = now + std::chrono::seconds(60);
    state->income_boost_factor = 2.0;
    send_balance_change(client, *state);
  } else if (powerup_name == "Discounter") {
    state->discount_until = now + std::chrono::minutes(5);
    state->discount_factor = 0.25;
  } else if (powerup_name == "repurchasePowerups") {
    std::vector<std::string> restored;
    for (const auto &used : state->used_powerups) {
      if (used != powerup_name) {
        restored.push_back(used);
      }
    }
    state->purchased_powerups.insert(state->purchased_powerups.end(),
                                     restored.begin(), restored.end());
    client.send("STATE_UPDATE", Value{{"type", "PURCHASED_POWERUPS"},
                                      {"value", state->purchased_powerups}});
  }
}

void TycoonRoom::handle_powerup_attack(blueboat::Client &client,
                                       const Value &data) {
  PlayerState *attacker_state = find_state(client.id);
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
  PlayerState *target_state = find_state(target_client->id);
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

  auto now = std::chrono::steady_clock::now();
  bool full_screen = false;
  std::string verb = "Attacked";
  if (powerup_name == "Icer") {
    target_state->frozen_until = now + std::chrono::seconds(15);
    full_screen = true;
    verb = "Froze";
  } else if (powerup_name == "Subtractor") {
    long long removed =
        round_to_ll(static_cast<double>(target_state->balance) * 0.20);
    target_state->balance -= removed;
    target_client->send(
        "STATE_UPDATE",
        Value{{"type", "BALANCE"}, {"value", target_state->balance}});
    verb = "Subtracted from";
  } else if (powerup_name == "Giving") {
    long long given =
        round_to_ll(static_cast<double>(target_state->balance) * 0.25);
    target_state->balance += given;
    target_client->send(
        "STATE_UPDATE",
        Value{{"type", "BALANCE"}, {"value", target_state->balance}});
    verb = "Gifted";
  } else if (powerup_name == "Blurred Screen") {
    verb = "Blurred";
  } else if (powerup_name == "outnumbered") {
    verb = "Outnumbered";
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
  PlayerState *state = find_state(client.id);
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
  PlayerState *state = find_state(client.id);
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
    const PlayerState *state = find_state(client_ptr->id);
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
      "LiveGame",
      [](Catalog catalog, Value default_questions, Value default_game_options,
         IntentRegistry &intent_registry) -> std::unique_ptr<blueboat::Room> {
        return std::make_unique<TycoonRoom>(
            std::move(catalog), std::move(default_questions),
            std::move(default_game_options), intent_registry);
      },
      resolve_tycoon_intent_extras,
  });
}

} // namespace openkit::tycoon
