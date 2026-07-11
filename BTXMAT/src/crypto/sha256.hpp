#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace superhero::crypto {

class Sha256 {
public:
    static constexpr size_t kDigestSize = 32;

    Sha256();
    void write(std::span<const uint8_t> data);
    void finalize(std::array<uint8_t, kDigestSize>& out);
    void reset();

    static std::array<uint8_t, kDigestSize> hash(std::span<const uint8_t> data);
    static std::array<uint8_t, kDigestSize> hash256d(std::span<const uint8_t> data);

private:
    void transform();
    std::vector<uint8_t> buffer_;
    uint64_t bit_count_{0};
    uint32_t state_[8];
};

void write_le16(uint8_t* out, uint16_t value);
void write_le32(uint8_t* out, uint32_t value);
void write_le64(uint8_t* out, uint64_t value);
uint32_t read_le32(const uint8_t* in);
uint64_t read_le64(const uint8_t* in);

}  // namespace superhero::crypto
