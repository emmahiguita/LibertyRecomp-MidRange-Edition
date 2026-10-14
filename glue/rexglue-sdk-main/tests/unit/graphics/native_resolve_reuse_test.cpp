#include <catch2/catch_test_macros.hpp>
#include "graphics/gta4_native/native_resolve_reuse.h"
#include <array>
#include <random>
using namespace rex::graphics::gta4_native;
namespace {
NativeResolveReuseKey Key() {
  return {.recording=1,.source_image=2,.source_view=3,.source_lifetime=4,.source_writer=5,
          .destination_image=6,.destination_lifetime=7,.destination_generation=8,
          .operation=3,.source_format=109,.destination_format=97,.physical_samples=1,
          .content_samples=0,.requested_samples=0,.sample_select=0,.flags=0,.mip=0,.exponent=0,
          .source_origin={1,2},.destination_origin={3,4},.source_extent={20,10},
          .destination_extent={20,10},.source_image_extent={64,48},.destination_image_extent={64,48}};
}
}
TEST_CASE("Resolve reuse requires complete unchanged operation and content identity", "[resolve-reuse]") {
  NativeResolveReuseRecord record; auto original=Key(); CHECK_FALSE(record.Matches(original,10));
  record.Commit(original,10); CHECK(record.Matches(original,10)); CHECK_FALSE(record.Matches(original,11));
  CHECK_FALSE(record.Matches(original,0));
  for (auto member : {&NativeResolveReuseKey::recording,&NativeResolveReuseKey::source_image,
       &NativeResolveReuseKey::source_view,&NativeResolveReuseKey::source_lifetime,&NativeResolveReuseKey::source_writer,
       &NativeResolveReuseKey::destination_image,&NativeResolveReuseKey::destination_lifetime,&NativeResolveReuseKey::destination_generation}) {
    auto other=original; ++(other.*member); CHECK_FALSE(record.Matches(other,10));
  }
  for (auto member : {&NativeResolveReuseKey::operation,&NativeResolveReuseKey::source_format,
       &NativeResolveReuseKey::destination_format,&NativeResolveReuseKey::physical_samples,
       &NativeResolveReuseKey::content_samples,&NativeResolveReuseKey::requested_samples,
       &NativeResolveReuseKey::sample_select,&NativeResolveReuseKey::flags,&NativeResolveReuseKey::mip}) {
    auto other=original; ++(other.*member); CHECK_FALSE(record.Matches(other,10));
  }
  auto other=original; ++other.exponent; CHECK_FALSE(record.Matches(other,10));
  for (auto member : {&NativeResolveReuseKey::source_origin,&NativeResolveReuseKey::destination_origin})
    for (size_t i=0;i<2;++i) { auto changed=original; ++(changed.*member)[i]; CHECK_FALSE(record.Matches(changed,10)); }
  for (auto member : {&NativeResolveReuseKey::source_extent,&NativeResolveReuseKey::destination_extent,
       &NativeResolveReuseKey::source_image_extent,&NativeResolveReuseKey::destination_image_extent})
    for (size_t i=0;i<2;++i) { auto changed=original; ++(changed.*member)[i]; CHECK_FALSE(record.Matches(changed,10)); }
}
TEST_CASE("Resolve reuse cannot survive abandoned recordings or recycled images", "[resolve-reuse]") {
  auto key=Key(); NativeResolveReuseRecord record;record.Commit(key,1);
  for (auto member : {&NativeResolveReuseKey::recording,&NativeResolveReuseKey::source_image,
       &NativeResolveReuseKey::source_lifetime,&NativeResolveReuseKey::source_writer,
       &NativeResolveReuseKey::destination_image,&NativeResolveReuseKey::destination_lifetime,&NativeResolveReuseKey::destination_generation}) {
    auto invalid=key; invalid.*member=0; CHECK_FALSE(invalid.valid()); CHECK_FALSE(record.Matches(invalid,1));
  }
  auto invalid=key;invalid.recording=UINT64_MAX;CHECK_FALSE(record.Matches(invalid,1));
  invalid=key;invalid.destination_image=invalid.source_image;CHECK_FALSE(invalid.valid());
  for (size_t n=0;n<2;++n) { invalid=key;invalid.source_extent[n]=0;CHECK_FALSE(invalid.valid());
    invalid=key;invalid.destination_extent[n]=0;CHECK_FALSE(invalid.valid()); }
  std::mt19937 random(9227);
  for (size_t n=0;n<10000;++n) {
    ++key.recording;key.source_writer=random()+uint64_t{1};key.destination_generation=random()+uint64_t{1};
    CHECK_FALSE(record.Matches(key,1));record.Commit(key,1);CHECK(record.Matches(key,1));
    ++key.source_writer;CHECK_FALSE(record.Matches(key,1));record.Commit(key,2);
    CHECK_FALSE(record.Matches(key,1));CHECK(record.Matches(key,2));
  }
}
TEST_CASE("HDR sharing requires the identical single-level FP16 resolve output", "[resolve-reuse]") {
  CHECK(CanUseNativeResolvedColorAsHDRMirror(VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_ASPECT_COLOR_BIT,VK_SAMPLE_COUNT_1_BIT,1));
  for (auto f : {VK_FORMAT_UNDEFINED,VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16B16A16_UNORM})
    CHECK_FALSE(CanUseNativeResolvedColorAsHDRMirror(f,VK_IMAGE_ASPECT_COLOR_BIT,VK_SAMPLE_COUNT_1_BIT,1));
  for (auto a : {VkImageAspectFlags{0},VkImageAspectFlags{VK_IMAGE_ASPECT_DEPTH_BIT},VkImageAspectFlags{VK_IMAGE_ASPECT_COLOR_BIT|VK_IMAGE_ASPECT_DEPTH_BIT}})
    CHECK_FALSE(CanUseNativeResolvedColorAsHDRMirror(VK_FORMAT_R16G16B16A16_SFLOAT,a,VK_SAMPLE_COUNT_1_BIT,1));
  for (auto samples : {VK_SAMPLE_COUNT_2_BIT,VK_SAMPLE_COUNT_4_BIT,VK_SAMPLE_COUNT_8_BIT})
    CHECK_FALSE(CanUseNativeResolvedColorAsHDRMirror(VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_ASPECT_COLOR_BIT,samples,1));
  for (uint32_t mips : {0,2,8})
    CHECK_FALSE(CanUseNativeResolvedColorAsHDRMirror(VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_ASPECT_COLOR_BIT,VK_SAMPLE_COUNT_1_BIT,mips));
}
