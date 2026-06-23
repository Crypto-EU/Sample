#pragma once

#include "superminer.hpp"

#include <vector>

namespace sm {

std::vector<GpuProfile> detect_amd_devices(const std::vector<int>& requested);

}  // namespace sm
