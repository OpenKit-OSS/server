#include "openkit/experience_registry.hpp"

#include "blueboat/common/json.hpp"
#include "openkit/data_archive.hpp"

namespace openkit {

ExperienceRegistry ExperienceRegistry::load() {
  ExperienceRegistry registry;
  auto entry = data_archive().find("experiences.json");
  if (!entry) {
    return registry;
  }

  blueboat::Value list =
      blueboat::Value::parse(data_archive().load_text(*entry));
  for (const auto &entry_value : list) {
    std::string experience_id =
        entry_value.value("experienceId", std::string());
    if (experience_id.empty())
      continue;
    ExperienceInfo info{experience_id,
                        entry_value.value("mapId", std::string())};
    registry.by_id_[experience_id] = std::move(info);
  }
  return registry;
}

std::optional<ExperienceInfo>
ExperienceRegistry::find(const std::string &experience_id) const {
  auto it = by_id_.find(experience_id);
  if (it == by_id_.end())
    return std::nullopt;
  return it->second;
}

} // namespace openkit
