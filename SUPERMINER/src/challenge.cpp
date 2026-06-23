#include "superminer.hpp"

#include "challenge.hpp"

#include <dlfcn.h>

extern "C" {
int superminer_challenge_solve(const unsigned char* seed, int difficulty, uint64_t* out_nonce);
}

namespace sm {

bool solve_pearl_challenge(const uint8_t seed[32], int difficulty, uint64_t* nonce) {
  void* lib = dlopen("libsuperminer_share.so", RTLD_LAZY | RTLD_GLOBAL);
  if (!lib) {
    lib = dlopen("./libsuperminer_share.so", RTLD_LAZY | RTLD_GLOBAL);
  }
  if (!lib) {
    return superminer_challenge_solve(seed, difficulty, nonce) == 0;
  }
  using Fn = int (*)(const unsigned char*, int, uint64_t*);
  auto fn = reinterpret_cast<Fn>(dlsym(lib, "superminer_challenge_solve"));
  if (!fn) {
    return false;
  }
  return fn(seed, difficulty, nonce) == 0;
}

}  // namespace sm
