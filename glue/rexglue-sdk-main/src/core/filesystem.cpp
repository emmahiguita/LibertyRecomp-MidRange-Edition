/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <algorithm>

#include <rex/filesystem.h>
#include <rex/platform/env.h>

namespace rex {
namespace filesystem {

bool CreateParentFolder(const std::filesystem::path& path) {
  if (path.has_parent_path()) {
    auto parent_path = path.parent_path();
    if (!std::filesystem::exists(parent_path)) {
      return std::filesystem::create_directories(parent_path);
    }
  }
  return true;
}

std::filesystem::path GetResourcesFolder() {
  if (auto override_dir = rex::platform::env::get("REX_RESOURCES_DIR")) {
    if (!override_dir->empty()) {
      return std::filesystem::path(*override_dir);
    }
  }
  return GetExecutableFolder().parent_path() / "Resources";
}

}  // namespace filesystem
}  // namespace rex
