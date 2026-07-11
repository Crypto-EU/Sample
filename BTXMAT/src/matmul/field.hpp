#pragma once

#include "crypto/uint256.hpp"

#include <cstdint>

namespace superhero::matmul::field {

using Element = uint32_t;
constexpr Element kModulus = 0x7fffffffU;

Element add(Element a, Element b);
Element sub(Element a, Element b);
Element mul(Element a, Element b);
Element from_uint32(uint32_t x);
Element from_oracle(const crypto::Uint256& seed, uint32_t index);
Element dot(const Element* a, const Element* b, uint32_t len);

}  // namespace superhero::matmul::field
