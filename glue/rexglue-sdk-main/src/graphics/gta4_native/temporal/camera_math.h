#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace rex::graphics::gta4_native::temporal {
using CameraMatrix = std::array<float, 16>;
// Title cameras use row vectors and row-major matrices.
inline std::optional<CameraMatrix> InverseCameraMatrix(const CameraMatrix& input) {
  double values[4][8]{};
  for (size_t row = 0; row < 4; ++row) for (size_t col = 0; col < 4; ++col) {
    if (!std::isfinite(input[row * 4 + col])) return {};
    values[row][col] = input[row * 4 + col]; values[row][col + 4] = row == col;
  }
  for (size_t col = 0; col < 4; ++col) {
    size_t pivot = col;
    for (size_t row = col + 1; row < 4; ++row)
      if (std::abs(values[row][col]) > std::abs(values[pivot][col])) pivot = row;
    if (std::abs(values[pivot][col]) < 1.0e-12) return {};
    for (size_t k = 0; k < 8; ++k) std::swap(values[col][k], values[pivot][k]);
    const double scale = values[col][col];
    for (auto& value : values[col]) value /= scale;
    for (size_t row = 0; row < 4; ++row) if (row != col) {
      const double factor = values[row][col];
      for (size_t k = 0; k < 8; ++k) values[row][k] -= factor * values[col][k];
    }
  }
  CameraMatrix result{};
  for (size_t row = 0; row < 4; ++row) for (size_t col = 0; col < 4; ++col) {
    result[row * 4 + col] = float(values[row][col + 4]);
    if (!std::isfinite(result[row * 4 + col])) return {};
  }
  return result;
}
inline CameraMatrix MultiplyCameraMatrices(const CameraMatrix& left, const CameraMatrix& right) {
  CameraMatrix result{};
  for (size_t row = 0; row < 4; ++row) for (size_t col = 0; col < 4; ++col) {
    double sum = 0;
    for (size_t k = 0; k < 4; ++k) sum += double(left[row * 4 + k]) * right[k * 4 + col];
    result[row * 4 + col] = float(sum);
  }
  return result;
}
}  // namespace rex::graphics::gta4_native::temporal
