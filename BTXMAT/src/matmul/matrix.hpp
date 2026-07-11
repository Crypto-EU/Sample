#pragma once

#include "matmul/field.hpp"
#include "crypto/uint256.hpp"

#include <cstdint>
#include <vector>

namespace superhero::matmul {

class ConstMatrixView {
public:
    ConstMatrixView(const field::Element* data, uint32_t rows, uint32_t cols, uint32_t stride);

    const field::Element& at(uint32_t row, uint32_t col) const;
    const field::Element* row_ptr(uint32_t row) const;
    uint32_t rows() const { return rows_; }
    uint32_t cols() const { return cols_; }

private:
    const field::Element* data_;
    uint32_t rows_;
    uint32_t cols_;
    uint32_t stride_;
};

class Matrix {
public:
    Matrix(uint32_t rows, uint32_t cols);

    field::Element& at(uint32_t row, uint32_t col);
    const field::Element& at(uint32_t row, uint32_t col) const;

    uint32_t rows() const { return rows_; }
    uint32_t cols() const { return cols_; }
    field::Element* data() { return data_.data(); }
    const field::Element* data() const { return data_.data(); }

    Matrix block(uint32_t bi, uint32_t bj, uint32_t b) const;
    ConstMatrixView block_view(uint32_t bi, uint32_t bj, uint32_t b) const;
    void set_block(uint32_t bi, uint32_t bj, uint32_t b, const Matrix& blk);

    Matrix operator+(const Matrix& rhs) const;
    Matrix operator-(const Matrix& rhs) const;
    Matrix operator*(const Matrix& rhs) const;

private:
    uint32_t rows_;
    uint32_t cols_;
    std::vector<field::Element> data_;
};

Matrix from_seed(const crypto::Uint256& seed, uint32_t n);
Matrix low_rank_product(const Matrix& left, const Matrix& right);
Matrix multiply_blocked(const Matrix& lhs, const Matrix& rhs, uint32_t tile_size);

}  // namespace superhero::matmul
