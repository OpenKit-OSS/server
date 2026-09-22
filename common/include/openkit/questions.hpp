#pragma once

#include "blueboat/common/json.hpp"

namespace openkit {

using blueboat::Value;

Value expand_questions(const Value &simple_questions);

} // namespace openkit
