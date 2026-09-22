#include "openkit/questions.hpp"

#include <stdexcept>

#include "blueboat/common/random_id.hpp"

namespace openkit {

Value expand_questions(const Value &simple_questions) {
  if (!simple_questions.is_array()) {
    throw std::runtime_error("openkit: question list must be a JSON array");
  }

  Value expanded = Value::array();
  for (const auto &q : simple_questions) {
    if (!q.contains("text") || !q.contains("answers") ||
        !q.at("answers").is_array()) {
      throw std::runtime_error("openkit: each question needs a \"text\" string "
                               "and an \"answers\" array");
    }

    Value correct_indices = Value::array({0});
    if (q.contains("correct")) {
      correct_indices = q.at("correct").is_array()
                            ? q.at("correct")
                            : Value::array({q.at("correct")});
    }

    Value answers = Value::array();
    int index = 0;
    for (const auto &answer_text : q.at("answers")) {
      bool is_correct = false;
      for (const auto &correct_index : correct_indices) {
        if (correct_index.get<int>() == index) {
          is_correct = true;
          break;
        }
      }
      answers.push_back(Value{{"_id", blueboat::random_id()},
                              {"text", answer_text},
                              {"correct", is_correct}});
      index += 1;
    }

    expanded.push_back(Value{{"_id", blueboat::random_id()},
                             {"text", q.at("text")},
                             {"type", "mc"},
                             {"answers", answers}});
  }
  return expanded;
}

} // namespace openkit
