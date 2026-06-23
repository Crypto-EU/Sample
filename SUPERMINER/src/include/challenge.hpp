#pragma once

#include <cstdint>

namespace sm {

bool solve_pearl_challenge(const uint8_t seed[32], int difficulty, uint64_t* nonce);

}  // namespace sm
