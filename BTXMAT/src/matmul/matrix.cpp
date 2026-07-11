#include "matmul/matrix.hpp"

#include <algorithm>
#include <stdexcept>
#include <thread>

namespace superhero::matmul {

ConstMatrixView::ConstMatrixView(const field::Element* data, uint32_t rows, uint32_t cols, uint32_t stride)
    : data_(data), rows_(rows), cols_(cols), stride_(stride) {}

const field::Element& ConstMatrixView::at(uint32_t row, uint32_t col) const {
    return data_[static_cast<size_t>(row) * stride_ + col];
}

const field::Element* ConstMatrixView::row_ptr(uint32_t row) const {
    return &data_[static_cast<size_t>(row) * stride_];
}

Matrix::Matrix(uint32_t rows, uint32_t cols) : rows_(rows), cols_(cols), data_(static_cast<size_t>(rows) * cols, 0) {}

field::Element& Matrix::at(uint32_t row, uint32_t col) {
    return data_[static_cast<size_t>(row) * cols_ + col];
}

const field::Element& Matrix::at(uint32_t row, uint32_t col) const {
    return data_[static_cast<size_t>(row) * cols_ + col];
}

Matrix Matrix::block(uint32_t bi, uint32_t bj, uint32_t b) const {
    Matrix out(b, b);
    const uint32_t row0 = bi * b;
    const uint32_t col0 = bj * b;
    for (uint32_t r = 0; r < b; ++r) {
        for (uint32_t c = 0; c < b; ++c) {
            out.at(r, c) = at(row0 + r, col0 + c);
        }
    }
    return out;
}

ConstMatrixView Matrix::block_view(uint32_t bi, uint32_t bj, uint32_t b) const {
    const uint32_t row0 = bi * b;
    const uint32_t col0 = bj * b;
    return ConstMatrixView(&data_[static_cast<size_t>(row0) * cols_ + col0], b, b, cols_);
}

void Matrix::set_block(uint32_t bi, uint32_t bj, uint32_t b, const Matrix& blk) {
    const uint32_t row0 = bi * b;
    const uint32_t col0 = bj * b;
    for (uint32_t r = 0; r < b; ++r) {
        for (uint32_t c = 0; c < b; ++c) {
            at(row0 + r, col0 + c) = blk.at(r, c);
        }
    }
}

Matrix Matrix::operator+(const Matrix& rhs) const {
    Matrix out(rows_, cols_);
    for (size_t i = 0; i < data_.size(); ++i) {
        out.data_[i] = field::add(data_[i], rhs.data_[i]);
    }
    return out;
}

Matrix Matrix::operator-(const Matrix& rhs) const {
    Matrix out(rows_, cols_);
    for (size_t i = 0; i < data_.size(); ++i) {
        out.data_[i] = field::sub(data_[i], rhs.data_[i]);
    }
    return out;
}

Matrix Matrix::operator*(const Matrix& rhs) const {
    Matrix out(rows_, rhs.cols_);
    std::vector<field::Element> col(rhs.rows_);
    for (uint32_t i = 0; i < rows_; ++i) {
        const field::Element* row_ptr = &data_[static_cast<size_t>(i) * cols_];
        for (uint32_t j = 0; j < rhs.cols_; ++j) {
            for (uint32_t k = 0; k < rhs.rows_; ++k) col[k] = rhs.at(k, j);
            out.at(i, j) = field::dot(row_ptr, col.data(), cols_);
        }
    }
    return out;
}

Matrix from_seed(const crypto::Uint256& seed, uint32_t n) {
    Matrix out(n, n);
    for (uint32_t row = 0; row < n; ++row) {
        for (uint32_t col = 0; col < n; ++col) {
            out.at(row, col) = field::from_oracle(seed, row * n + col);
        }
    }
    return out;
}

Matrix low_rank_product(const Matrix& left, const Matrix& right) {
    Matrix out(left.rows(), right.cols());
    for (uint32_t i = 0; i < left.rows(); ++i) {
        for (uint32_t j = 0; j < right.cols(); ++j) {
            field::Element acc = 0;
            for (uint32_t k = 0; k < left.cols(); ++k) {
                acc = field::add(acc, field::mul(left.at(i, k), right.at(k, j)));
            }
            out.at(i, j) = acc;
        }
    }
    return out;
}

Matrix multiply_blocked(const Matrix& lhs, const Matrix& rhs, uint32_t tile_size) {
    if (tile_size == 0) throw std::runtime_error("tile size must be non-zero");
    Matrix out(lhs.rows(), rhs.cols());
    const uint32_t lhs_rows = lhs.rows();
    const uint32_t lhs_cols = lhs.cols();
    const uint32_t rhs_cols = rhs.cols();
    const uint32_t row_tiles = (lhs_rows + tile_size - 1) / tile_size;
    const uint32_t workers = std::max<uint32_t>(1, std::min(row_tiles, std::thread::hardware_concurrency()));

    auto process = [&](uint32_t tile_begin, uint32_t tile_end) {
        for (uint32_t tile = tile_begin; tile < tile_end; ++tile) {
            const uint32_t ii = tile * tile_size;
            const uint32_t i_end = std::min(ii + tile_size, lhs_rows);
            for (uint32_t kk = 0; kk < lhs_cols; kk += tile_size) {
                const uint32_t k_end = std::min(kk + tile_size, lhs_cols);
                for (uint32_t jj = 0; jj < rhs_cols; jj += tile_size) {
                    const uint32_t j_end = std::min(jj + tile_size, rhs_cols);
                    for (uint32_t i = ii; i < i_end; ++i) {
                        for (uint32_t k = kk; k < k_end; ++k) {
                            const field::Element a_ik = lhs.at(i, k);
                            for (uint32_t j = jj; j < j_end; ++j) {
                                out.at(i, j) = field::add(out.at(i, j), field::mul(a_ik, rhs.at(k, j)));
                            }
                        }
                    }
                }
            }
        }
    };

    if (workers <= 1) {
        process(0, row_tiles);
        return out;
    }

    const uint32_t tiles_per_worker = (row_tiles + workers - 1) / workers;
    std::vector<std::thread> threads;
    for (uint32_t w = 0; w < workers; ++w) {
        const uint32_t begin = w * tiles_per_worker;
        if (begin >= row_tiles) break;
        const uint32_t end = std::min(begin + tiles_per_worker, row_tiles);
        threads.emplace_back(process, begin, end);
    }
    for (auto& t : threads) t.join();
    return out;
}

}  // namespace superhero::matmul
