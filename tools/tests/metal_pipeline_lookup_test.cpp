#include "graphics/gta4_metal/pipeline_lookup_memo.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
using rex::graphics::gta4_metal::PipelineLookupMemo;
struct Key {std::array<uint64_t,64> words{};bool operator==(const Key&)const=default;};
struct Hash {size_t operator()(const Key&)const{return 0;}}; // Force collisions.
struct Pipeline {unsigned identity;bool ready=false;};
int main(){
  PipelineLookupMemo<Key,Pipeline> memo;
  Key key{};Pipeline a{1},b{2};assert(!memo.Find(key));
  memo.Remember(key,nullptr);assert(!memo.Find(key));
  memo.Remember(key,&a);assert(memo.Find(key)==&a);
  for(size_t i=0;i<key.words.size();++i){auto changed=key;changed.words[i]=1;assert(!memo.Find(changed));}
  // Ready/failure state belongs to the pipeline, not the memo.
  assert(!memo.Find(key)->ready);a.ready=true;assert(memo.Find(key)->ready);
  std::unordered_map<Key,Pipeline,Hash> owner;
  for(unsigned i=0;i<16;++i){auto k=key;k.words[0]=i;auto [it,ok]=owner.emplace(k,Pipeline{i});assert(ok);memo.Remember(k,&it->second);}
  for(unsigned i=0;i<16;++i){auto k=key;k.words[0]=i;auto* p=memo.Find(k);assert((i>=12)==bool(p));if(p)assert(p->identity==i);}
  const auto last=owner.find(Key{{15}});assert(last!=owner.end());auto* stable=&last->second;
  owner.rehash(10000);assert(memo.Find(Key{{15}})==stable);
  // Owner invalidates before erasing: no stale pointer may be returned.
  memo.Reset();owner.erase(Key{{15}});assert(!memo.Find(Key{{15}}));
  memo.Remember(key,&b);assert(memo.Find(key)==&b);memo.Reset();assert(!memo.Find(key));
  PipelineLookupMemo<Key,Pipeline> other;assert(!other.Find(key));
  std::puts("metal_pipeline_lookup=passed exact_keys=true collisions=true rehash=true eviction=true");
}
