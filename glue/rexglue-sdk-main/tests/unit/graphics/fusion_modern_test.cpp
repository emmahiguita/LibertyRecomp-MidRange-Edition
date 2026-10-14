#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <rex/graphics/gta4_native/fusion_timecycle.h>
#include <rex/graphics/gta4_native/modern_effect_constants.h>
#include "graphics/gta4_native/sun_shafts_parameters.h"
#include "graphics/gta4_native/modern_shader_policy.h"
#include "graphics/gta4_native/stateful_constant_state.h"
#include <limits>
using namespace rex::graphics::gta4_native;

TEST_CASE("Fusion weather uses the authored knots and wraps midnight", "[modern-shaders][fusion]") {
  struct Case { float hour; uint32_t old_weather, new_weather; float blend; std::array<float, 5> expected; };
  // Independently evaluated from the pinned text data with Python double arithmetic.
  const Case cases[] = {
    {0.0f, 0, 0, 0.0f, {0.004f, 0.015f, 0.7f, 0.55f, 0.0f}},
    {5.0f, 0, 0, 0.0f, {0.004f, 0.015f, 0.7f, 0.55f, 0.0f}},
    {6.5f, 1, 3, 0.4f, {0.0042f, 0.014f, 0.7f, 1.4f, 0.0013445f}},
    {23.0f, 0, 1, 0.5f, {0.004f, 0.0125f, 0.7f, 1.175f, 0.0f}},
    {12.0f, 8, 8, 1.0f, {0.004f, 0.015f, 0.7f, 1.0f, 0.0f}},
    {25.5f, 7, 8, 0.3f, {0.0082f, 0.015f, 0.77f, 1.0f, 0.0f}},
    {-1.0f, 2, 5, 0.7f, {0.0082f, 0.015f, 0.77f, 0.64f, 0.0f}},
  };
  for (const auto& c : cases) {
    FusionTimecycle result;
    REQUIRE(SampleFusionTimecycle(c.hour, c.old_weather, c.new_weather, c.blend, result));
    for (size_t i = 0; i < 5; ++i) CHECK(result.values[i] == Catch::Approx(c.expected[i]).margin(1.0e-7));
  }
  FusionTimecycle result;
  CHECK_FALSE(SampleFusionTimecycle(0, 9, 0, 0, result));
  CHECK_FALSE(SampleFusionTimecycle(std::numeric_limits<float>::quiet_NaN(), 0, 0, 0, result));
}
TEST_CASE("Fusion modifiers preserve omitted values and support zero intensity", "[modern-shaders][fusion]") {
  FusionTimecycle v{{1,2,3,4,5}}, m{{-1,0,5,-1,0}};
  ApplyFusionModifier(v, m, 0.25f);
  CHECK(v.values == std::array<float,5>{1,1.5f,3.5f,4,3.75f});
  ApplyFusionModifier(v, m, 1.0f);
  CHECK(v.values == std::array<float,5>{1,0,5,4,0});
  CHECK(FindFusionModifier(0xFFFFFFFFu) == nullptr);
}
TEST_CASE("Environmental camera inversion preserves asymmetric perspective and translation", "[modern-shaders][fusion]") {
  const std::array<float,16> matrix{2,0,0,0, 0,3,0,0, 0.2f,-0.1f,-1,-1, -4,6,-0.2f,0};
  std::array<float,16> inverse{};
  REQUIRE(InvertEnvironmentalMatrix(matrix, inverse));
  for (size_t row=0; row<4; ++row) for(size_t col=0; col<4; ++col) {
    double value=0;
    for(size_t k=0;k<4;++k) value += double(matrix[row*4+k])*inverse[k*4+col];
    CHECK(value == Catch::Approx(row==col?1.0:0.0).margin(0.00001));
  }
  CHECK_FALSE(InvertEnvironmentalMatrix({}, inverse));
  auto invalid=matrix; invalid[0]=std::numeric_limits<float>::infinity();
  CHECK_FALSE(InvertEnvironmentalMatrix(invalid,inverse));
}
TEST_CASE("Both renderers reject nonfinite extended environment values", "[modern-shaders][fusion]") {
  EnvironmentalDataV2 e; e.byte_size=sizeof(e); e.source_sequence=1;
  e.valid_fields=EnvironmentalFieldBit(EnvironmentalField::kSkyColorExposure);
  REQUIRE(ValidEnvironmentalData(e));
  e.sky_color_exposure[3]=std::numeric_limits<float>::infinity();
  CHECK_FALSE(ValidEnvironmentalData(e));
  e.valid_fields=0; CHECK(ValidEnvironmentalData(e));
  e.version=1; CHECK_FALSE(ValidEnvironmentalData(e));
}
TEST_CASE("Modern shared constants cannot reuse a different LUT or viewport", "[modern-shaders][fusion]") {
  SharedConstantSemanticKey<26> a,b;
  REQUIRE(a==b); b.tone_lut_address=8; CHECK_FALSE(a==b);
  b=a; b.modern_effects_enabled=1; CHECK_FALSE(a==b);
  b=a; b.cloud_mask_address=8; CHECK_FALSE(a==b);
  b=a; b.viewport_bits[3]=1; CHECK_FALSE(a==b);
  b=a; b.water_reflection=1; CHECK_FALSE(a==b);
  CHECK(BuildModernEffectConstants(false, 8, nullptr).tone_lut_address==0);
  CHECK(BuildModernEffectConstants(true, 8, nullptr).tone_lut_address==8);
}
