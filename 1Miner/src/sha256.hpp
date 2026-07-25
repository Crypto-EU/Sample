#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace oneminer {

using Hash256 = std::array<uint8_t, 32>;
using Sha256State = std::array<uint32_t, 8>;

void sha256(const uint8_t* data, size_t len, Hash256& out);
Hash256 sha256(const std::string& s);
Hash256 sha256(const std::vector<uint8_t>& v);

// Incremental API for midstate mining.
void sha256_init(Sha256State& state);
void sha256_transform(Sha256State& state, const uint8_t block[64]);
void sha256_finalize_into(Sha256State state, const uint8_t* tail, size_t tail_len,
                          uint64_t total_len, Hash256& out);

std::string hash_to_hex(const Hash256& h);
bool hex_to_bytes(const std::string& hex, std::vector<uint8_t>& out);

}  // namespace oneminer
