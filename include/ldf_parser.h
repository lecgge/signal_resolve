#pragma once

#include <filesystem>
#include "usde_types.h"

namespace usde {

bool LoadLDF(const std::filesystem::path& file_path, NetworkCluster& out_cluster);

} // namespace usde
