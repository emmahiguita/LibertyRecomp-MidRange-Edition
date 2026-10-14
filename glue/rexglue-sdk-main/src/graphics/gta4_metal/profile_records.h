#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include "resolve_reuse.h"
namespace rex::graphics::gta4_metal {
// Describes existing encoders; never creates a render pass or timestamp query.
struct ScopeMemberKey {
  uint64_t vertex=0,pixel=0,variant=0;
  uint32_t phase=0,samples=0;
  bool operator==(const ScopeMemberKey&) const=default;
};
struct ScopeMember {
  ScopeMemberKey key{};
  uint64_t draws=0,vertices=0,indices=0;
};
class ScopeProfile {
 public:
  void Begin(uint64_t recording,uint64_t scope,uint32_t width,uint32_t height) {
    recording_=recording;scope_=scope;width_=width;height_=height;size_=0;
    last_=members_.size();draws_=overflow_=0;active_=true;
  }
  void Draw(const ScopeMemberKey& key,uint32_t vertices,uint32_t indices) {
    if(!active_)return;
    ++draws_;
    size_t slot=last_;
    if(slot>=size_ || !(members_[slot].key==key)) {
      for(slot=0;slot<size_ && !(members_[slot].key==key);++slot) {}
      if(slot==size_) {
        if(size_==members_.size()) {++overflow_;last_=members_.size();return;}
        members_[size_++]={key,0,0,0};
      }
    }
    last_=slot;auto& value=members_[slot];++value.draws;value.vertices+=vertices;value.indices+=indices;
  }
  void End(){active_=false;}
  bool active() const{return active_;}
  uint64_t recording() const{return recording_;}
  uint64_t scope() const{return scope_;}
  uint64_t draws() const{return draws_;}
  uint64_t overflow() const{return overflow_;}
  uint32_t width() const{return width_;} uint32_t height() const{return height_;}
  std::span<const ScopeMember> members() const{return {members_.data(),size_};}
 private:
  std::array<ScopeMember,64> members_{};
  size_t size_=0,last_=64;
  uint64_t recording_=0,scope_=0,draws_=0,overflow_=0;
  uint32_t width_=0,height_=0;
  bool active_=false;
};
struct ResolveProfileRecord {
  ResolveReuseKey key{};
  uint64_t sequence=0,destination_writer=0;
  uint32_t source=0,destination=0,clear_flags=0;
  bool reused=false;
  uint64_t first_read=0;
  uint32_t read_kind=0;
};
class ResolveProfile {
 public:
  void Begin(){size_=0;sequence_=0;overflow_=0;}
  void Write(const ResolveReuseKey& key,uint64_t writer,uint32_t source,uint32_t destination,
             uint32_t clear_flags,bool reused) {
    ++sequence_;
    if(size_==records_.size()) {++overflow_;return;}
    records_[size_++]={key,sequence_,writer,source,destination,clear_flags,reused,0,0};
  }
  // Readiness observed at a CPU binding, not proof that a fragment samples every
  // texel. The exact subresource write version is supplied by the resource owner.
  void Read(uint64_t generation,uint32_t level,uint32_t slice,uint64_t writer,uint32_t kind) {
    ++sequence_;
    for(size_t i=size_;i>0;--i) {
      auto& value=records_[i-1];
      if(value.key.destination_generation==generation && value.key.level==level && value.key.slice==slice &&
         value.destination_writer==writer) {
        if(!value.first_read){value.first_read=sequence_;value.read_kind=kind;}
        break;
      }
    }
  }
  std::span<const ResolveProfileRecord> records() const{return {records_.data(),size_};}
  uint64_t overflow() const{return overflow_;}
 private:
  std::array<ResolveProfileRecord,256> records_{};
  size_t size_=0;
  uint64_t sequence_=0,overflow_=0;
};
}  // namespace rex::graphics::gta4_metal
