#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace rex::graphics::gta4_metal::temporal {
struct CameraProjection {
  float near_plane, far_plane, field_of_view, aspect;
  bool reversed;
};
inline std::optional<CameraProjection> DescribeProjection(const std::array<float, 16>& p) {
  for (float v : p)
    if (!std::isfinite(v)) return {};
  if (std::abs(p[15]) > 1.0e-6f || std::abs(p[11]) < 0.5f || std::abs(p[5]) < 1.0e-6f ||
      std::abs(p[0]) < 1.0e-6f)
    return {};
  const double z0 = -double(p[14]) / p[10], z1 = -double(p[14]) / (double(p[10]) - p[11]);
  if (!std::isfinite(z0) || !std::isfinite(z1)) return {};
  const float near = float(std::min(std::abs(z0), std::abs(z1))),
              far = float(std::max(std::abs(z0), std::abs(z1)));
  if (near <= 0 || far <= near || far > 1.0e8f) return {};
  constexpr double pi = 3.1415926535897932384626433832795;
  return CameraProjection{near, far, float(2 * std::atan(1.0 / std::abs(double(p[5]))) * 180 / pi),
                          std::abs(p[5] / p[0]), std::abs(z0) > std::abs(z1)};
}
inline std::array<float, 16> JitterProjection(const std::array<float, 16>& p,
                                              std::array<float, 2> pixels, uint32_t width,
                                              uint32_t height) {
  auto result = p;
  if (!width || !height) return result;
  const std::array<float, 2> clip{2 * pixels[0] / float(width), -2 * pixels[1] / float(height)};
  for (size_t row = 0; row < 4; ++row) {
    result[row * 4] += clip[0] * p[row * 4 + 3];
    result[row * 4 + 1] += clip[1] * p[row * 4 + 3];
  }
  return result;
}
inline bool CameraDiscontinuity(const std::array<float, 16>& old,
                                const std::array<float, 16>& next) {
  double distance = 0, rotation = 0;
  for (size_t i = 0; i < 3; ++i) {
    distance += std::pow(double(old[12 + i]) - next[12 + i], 2);
    for (size_t j = 0; j < 3; ++j) rotation += double(old[i * 4 + j]) * next[i * 4 + j];
  }
  return distance > 100.0 || rotation < 2.0;
}
// Title matrices use row vectors and row-major storage. Metal's column-major
// matrix load transposes this product, so the shader multiplies matrix * clip.
inline std::optional<std::array<float, 16>> BackgroundReprojection(
    const std::array<float, 16>& current_projection,
    std::array<float, 16> current_inverse_view,
    const std::array<float, 16>& previous_projection,
    std::array<float, 16> previous_inverse_view) {
  const auto inverse = [](const std::array<float,16>& input)
      -> std::optional<std::array<float,16>> {
    double a[4][8]{};
    for(size_t r=0;r<4;++r)for(size_t c=0;c<4;++c){
      if(!std::isfinite(input[r*4+c]))return {};
      a[r][c]=input[r*4+c];a[r][c+4]=r==c;
    }
    for(size_t c=0;c<4;++c){
      size_t pivot=c;
      for(size_t r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;
      if(std::abs(a[pivot][c])<1.0e-12)return {};
      for(size_t k=0;k<8;++k)std::swap(a[c][k],a[pivot][k]);
      const double scale=a[c][c];
      for(double& value:a[c])value/=scale;
      for(size_t r=0;r<4;++r)if(r!=c){
        const double factor=a[r][c];
        for(size_t k=0;k<8;++k)a[r][k]-=factor*a[c][k];
      }
    }
    std::array<float,16> result{};
    for(size_t r=0;r<4;++r)for(size_t c=0;c<4;++c){
      result[r*4+c]=float(a[r][c+4]);
      if(!std::isfinite(result[r*4+c]))return {};
    }
    return result;
  };
  const auto multiply = [](const auto& a,const auto& b){
    std::array<float,16> result{};
    for(size_t r=0;r<4;++r)for(size_t c=0;c<4;++c){
      double value=0;
      for(size_t k=0;k<4;++k)value+=double(a[r*4+k])*b[k*4+c];
      result[r*4+c]=float(value);
    }
    return result;
  };
  // Cleared depth represents an infinite environment. Camera translation
  // must not move that environment; rotation and projection still do.
  for(size_t i=12;i<15;++i)current_inverse_view[i]=previous_inverse_view[i]=0;
  const auto inverse_projection=inverse(current_projection);
  const auto previous_view=inverse(previous_inverse_view);
  if(!inverse_projection||!previous_view)return {};
  auto result=multiply(multiply(multiply(*inverse_projection,current_inverse_view),
                                *previous_view),previous_projection);
  for(float v:result)if(!std::isfinite(v))return {};
  return result;
}
}  // namespace rex::graphics::gta4_metal::temporal
