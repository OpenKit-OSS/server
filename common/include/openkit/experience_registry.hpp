#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace openkit {

struct ExperienceInfo {
  std::string experience_id;
  std::string map_id;
};

class ExperienceRegistry {
public:
  static ExperienceRegistry load();

  std::optional<ExperienceInfo> find(const std::string &experience_id) const;

private:
  std::unordered_map<std::string, ExperienceInfo> by_id_;
};

} // namespace openkit
