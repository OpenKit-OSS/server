#include "openkit/data_archive.hpp"

#include "bundle_openkit_data.h"

namespace openkit {

const bundle::archive &data_archive() {
  static const bundle::archive archive(bundle_openkit_data_archive());
  return archive;
}

} // namespace openkit
