#include "matmul/transcript.hpp"

#include "crypto/sha256.hpp"

#include <stdexcept>

namespace superhero::matmul::transcript {
namespace {

crypto::Uint256 derive_compression_seed(const crypto::Uint256& sigma) {
    uint8_t sigma_bytes[32];
    sigma.to_canonical_bytes(sigma_bytes);
    crypto::Sha256 hasher;
    hasher.write({reinterpret_cast<const uint8_t*>(kCompressTag.data()), kCompressTag.size()});
    hasher.write({sigma_bytes, sizeof(sigma_bytes)});
    std::array<uint8_t, 32> digest{};
    hasher.finalize(digest);
    return crypto::Uint256::from_be_bytes(digest.data());
}

ConstMatrixView row_block_all_cols(const Matrix& matrix, uint32_t block_index, uint32_t block_rows) {
    return ConstMatrixView(
        matrix.data() + static_cast<size_t>(block_index) * block_rows * matrix.cols(),
        block_rows, matrix.cols(), matrix.cols());
}

ConstMatrixView all_rows_col_block(const Matrix& matrix, uint32_t block_index, uint32_t block_cols) {
    return ConstMatrixView(
        matrix.data() + static_cast<size_t>(block_index) * block_cols,
        matrix.rows(), block_cols, matrix.cols());
}

size_t block_product_index(uint32_t i, uint32_t j, uint32_t ell, uint32_t blocks_per_axis) {
    return (static_cast<size_t>(i) * blocks_per_axis + j) * blocks_per_axis + ell;
}

field::Element compress_af_block(
    const ConstMatrixView& a_block,
    const ConstMatrixView& f_l_block,
    const ConstMatrixView& f_r_block,
    const std::vector<field::Element>& v) {
    const uint32_t b = a_block.rows();
    const uint32_t r = f_l_block.cols();
    std::vector<field::Element> weighted_v(static_cast<size_t>(b) * r, 0);
    for (uint32_t x = 0; x < b; ++x) {
        for (uint32_t u = 0; u < r; ++u) {
            field::Element acc = 0;
            for (uint32_t y = 0; y < b; ++y) {
                acc = field::add(acc, field::mul(v[static_cast<size_t>(x) * b + y], f_r_block.at(u, y)));
            }
            weighted_v[static_cast<size_t>(x) * r + u] = acc;
        }
    }
    field::Element scalar = 0;
    for (uint32_t t = 0; t < b; ++t) {
        for (uint32_t u = 0; u < r; ++u) {
            field::Element acc = 0;
            for (uint32_t x = 0; x < b; ++x) {
                acc = field::add(acc, field::mul(a_block.at(x, t), weighted_v[static_cast<size_t>(x) * r + u]));
            }
            scalar = field::add(scalar, field::mul(f_l_block.at(t, u), acc));
        }
    }
    return scalar;
}

field::Element compress_eb_block(
    const ConstMatrixView& e_l_block,
    const ConstMatrixView& e_r_block,
    const ConstMatrixView& b_block,
    const std::vector<field::Element>& v) {
    const uint32_t b = e_l_block.rows();
    const uint32_t r = e_l_block.cols();
    std::vector<field::Element> weighted_v(static_cast<size_t>(r) * b, 0);
    for (uint32_t u = 0; u < r; ++u) {
        for (uint32_t y = 0; y < b; ++y) {
            field::Element acc = 0;
            for (uint32_t x = 0; x < b; ++x) {
                acc = field::add(acc, field::mul(e_l_block.at(x, u), v[static_cast<size_t>(x) * b + y]));
            }
            weighted_v[static_cast<size_t>(u) * b + y] = acc;
        }
    }
    field::Element scalar = 0;
    for (uint32_t u = 0; u < r; ++u) {
        for (uint32_t t = 0; t < b; ++t) {
            field::Element acc = 0;
            for (uint32_t y = 0; y < b; ++y) {
                acc = field::add(acc, field::mul(weighted_v[static_cast<size_t>(u) * b + y], b_block.at(t, y)));
            }
            scalar = field::add(scalar, field::mul(e_r_block.at(u, t), acc));
        }
    }
    return scalar;
}

field::Element compress_ef_block(
    const ConstMatrixView& e_l_block,
    const ConstMatrixView& e_r_block,
    const ConstMatrixView& f_l_block,
    const ConstMatrixView& f_r_block,
    const std::vector<field::Element>& v) {
    const uint32_t b = e_l_block.rows();
    const uint32_t r = e_l_block.cols();
    std::vector<field::Element> weighted_v(static_cast<size_t>(r) * b, 0);
    for (uint32_t u = 0; u < r; ++u) {
        for (uint32_t y = 0; y < b; ++y) {
            field::Element acc = 0;
            for (uint32_t x = 0; x < b; ++x) {
                acc = field::add(acc, field::mul(e_l_block.at(x, u), v[static_cast<size_t>(x) * b + y]));
            }
            weighted_v[static_cast<size_t>(u) * b + y] = acc;
        }
    }
    std::vector<field::Element> weighted_fr(static_cast<size_t>(r) * r, 0);
    for (uint32_t u = 0; u < r; ++u) {
        for (uint32_t v_idx = 0; v_idx < r; ++v_idx) {
            field::Element acc = 0;
            for (uint32_t y = 0; y < b; ++y) {
                acc = field::add(acc, field::mul(weighted_v[static_cast<size_t>(u) * b + y], f_r_block.at(v_idx, y)));
            }
            weighted_fr[static_cast<size_t>(u) * r + v_idx] = acc;
        }
    }
    field::Element scalar = 0;
    for (uint32_t t = 0; t < b; ++t) {
        for (uint32_t v_idx = 0; v_idx < r; ++v_idx) {
            field::Element acc = 0;
            for (uint32_t u = 0; u < r; ++u) {
                acc = field::add(acc, field::mul(e_r_block.at(u, t), weighted_fr[static_cast<size_t>(u) * r + v_idx]));
            }
            scalar = field::add(scalar, field::mul(acc, f_l_block.at(t, v_idx)));
        }
    }
    return scalar;
}

class TranscriptHasher {
public:
    TranscriptHasher(const crypto::Uint256& sigma, uint32_t b)
        : b_(b), compress_vec_(derive_compression_vector(sigma, b)) {}

    void add_intermediate(const ConstMatrixView& block_bb) {
        const field::Element compressed = compress_block(block_bb, compress_vec_);
        uint8_t bytes[4];
        crypto::write_le32(bytes, compressed);
        hasher_.write({bytes, sizeof(bytes)});
    }

    crypto::Uint256 finalize() {
        std::array<uint8_t, 32> inner{};
        hasher_.finalize(inner);
        return crypto::Uint256(crypto::Sha256::hash256d(inner));
    }

private:
    uint32_t b_;
    crypto::Sha256 hasher_;
    std::vector<field::Element> compress_vec_;
};

}  // namespace

std::vector<field::Element> derive_compression_vector(const crypto::Uint256& sigma, uint32_t b) {
    if (b == 0) throw std::runtime_error("block size b must be non-zero");
    const crypto::Uint256 seed = derive_compression_seed(sigma);
    std::vector<field::Element> vec;
    const uint64_t len = static_cast<uint64_t>(b) * b;
    vec.reserve(static_cast<size_t>(len));
    for (uint64_t k = 0; k < len; ++k) {
        vec.push_back(field::from_oracle(seed, static_cast<uint32_t>(k)));
    }
    return vec;
}

field::Element compress_block(const ConstMatrixView& block_bb, const std::vector<field::Element>& v) {
    field::Element acc = 0;
    for (uint32_t row = 0; row < block_bb.rows(); ++row) {
        acc = field::add(acc, field::dot(block_bb.row_ptr(row), &v[static_cast<size_t>(row) * block_bb.cols()], block_bb.cols()));
    }
    return acc;
}

CanonicalResult canonical_matmul(const Matrix& a_prime, const Matrix& b_prime, uint32_t b, const crypto::Uint256& sigma) {
    const uint32_t n = a_prime.rows();
    const uint32_t N = n / b;
    Matrix c_prime(n, n);
    TranscriptHasher hasher(sigma, b);
    for (uint32_t i = 0; i < N; ++i) {
        for (uint32_t j = 0; j < N; ++j) {
            for (uint32_t ell = 0; ell < N; ++ell) {
                const Matrix product = a_prime.block(i, ell, b) * b_prime.block(ell, j, b);
                Matrix c_block = c_prime.block(i, j, b);
                c_block = c_block + product;
                c_prime.set_block(i, j, b, c_block);
                hasher.add_intermediate(c_prime.block_view(i, j, b));
            }
        }
    }
    return {.c_prime = std::move(c_prime), .transcript_hash = hasher.finalize()};
}

std::vector<Matrix> precompute_clean_block_products(const Matrix& a, const Matrix& b, uint32_t block_b) {
    const uint32_t n = a.rows();
    const uint32_t N = n / block_b;
    std::vector<Matrix> out;
    out.reserve(static_cast<size_t>(N) * N * N);
    for (uint32_t i = 0; i < N; ++i) {
        for (uint32_t j = 0; j < N; ++j) {
            for (uint32_t ell = 0; ell < N; ++ell) {
                out.push_back(a.block(i, ell, block_b) * b.block(ell, j, block_b));
            }
        }
    }
    return out;
}

crypto::Uint256 replay_canonical_hash_with_reusable_clean_products(
    const Matrix& a,
    const Matrix& b,
    const std::vector<Matrix>& clean_block_products,
    const noise::NoisePair& noise,
    uint32_t block_b,
    const crypto::Uint256& sigma) {
    const uint32_t n = a.rows();
    const uint32_t N = n / block_b;
    const auto compress_vec = derive_compression_vector(sigma, block_b);
    crypto::Sha256 hasher;

    for (uint32_t i = 0; i < N; ++i) {
        const ConstMatrixView e_l_block = row_block_all_cols(noise.e_l, i, block_b);
        for (uint32_t j = 0; j < N; ++j) {
            const ConstMatrixView f_r_block = all_rows_col_block(noise.f_r, j, block_b);
            field::Element compressed_prefix = 0;
            for (uint32_t ell = 0; ell < N; ++ell) {
                const Matrix& clean_block = clean_block_products[block_product_index(i, j, ell, N)];
                const field::Element clean_compressed = compress_block(
                    ConstMatrixView(clean_block.data(), clean_block.rows(), clean_block.cols(), clean_block.cols()),
                    compress_vec);
                const ConstMatrixView a_block = a.block_view(i, ell, block_b);
                const ConstMatrixView b_block = b.block_view(ell, j, block_b);
                const ConstMatrixView e_r_block = all_rows_col_block(noise.e_r, ell, block_b);
                const ConstMatrixView f_l_block = row_block_all_cols(noise.f_l, ell, block_b);
                const field::Element af = compress_af_block(a_block, f_l_block, f_r_block, compress_vec);
                const field::Element eb = compress_eb_block(e_l_block, e_r_block, b_block, compress_vec);
                const field::Element ef = compress_ef_block(e_l_block, e_r_block, f_l_block, f_r_block, compress_vec);
                compressed_prefix = field::add(
                    compressed_prefix,
                    field::add(clean_compressed, field::add(af, field::add(eb, ef))));
                uint8_t bytes[4];
                crypto::write_le32(bytes, compressed_prefix);
                hasher.write({bytes, sizeof(bytes)});
            }
        }
    }

    std::array<uint8_t, 32> inner{};
    hasher.finalize(inner);
    return crypto::Uint256(crypto::Sha256::hash256d(inner));
}

}  // namespace superhero::matmul::transcript
