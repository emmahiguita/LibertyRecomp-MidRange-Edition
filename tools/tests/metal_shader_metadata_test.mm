#include <cmath>
#import <Foundation/Foundation.h>
#include "graphics/gta4_metal/shaders.h"
#include "graphics/gta4_metal/shader_archive.h"
#include "ui/metal/context.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <new>
static std::atomic<size_t> allocations{0};static std::atomic<bool> count_allocations{false};
void* operator new(size_t size){if(void* p=std::malloc(size?size:1)){if(count_allocations.load(std::memory_order_relaxed))allocations.fetch_add(1,std::memory_order_relaxed);return p;}throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void* operator new[](size_t n){return ::operator new(n);}void operator delete[](void* p)noexcept{::operator delete(p);}
namespace {size_t checks=0;void Check(bool b,const std::string& message){++checks;if(!b)throw std::runtime_error(message);}}
int main(int argc,char** argv){@autoreleasepool{try{
 using namespace rex::graphics::gta4_metal;using rex::graphics::gta4_native::ShaderStage;
 Check(argc==2,"usage: metadata-test title_shader_archive.bin");std::string error;
 auto context=rex::ui::metal::MetalContext::Create(error);Check(bool(context),error);
 ShaderCache cache(context);Check(cache.InitializeFile(argv[1],error),error);
 std::ifstream input(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
 MetalShaderArchive archive;Check(archive.Open(std::as_bytes(std::span(bytes)),[](auto,auto){return true;},error),error);
 Check(!archive.records().empty(),"shader inventory is empty");
 std::vector<const ShaderMetadata*> pointers;
 for(const auto& record:archive.records()){
  const auto stage=ShaderStage(uint32_t(record.stage));const auto* metadata=cache.Metadata(record.hash,stage,error);Check(metadata!=nullptr,error);
  Check(metadata->name==record.name&&metadata->hash==record.hash&&metadata->used_texture_mask==record.used_texture_mask,"cached archive identity changed");
  Check(metadata->attribute_count==record.attribute_count&&metadata->attributes==record.attributes,"cached vertex interface changed");
  pointers.push_back(metadata);
 }
 for(size_t i=0;i<archive.records().size();++i){const auto& r=archive.records()[i];const auto stage=ShaderStage(uint32_t(r.stage));
  Check(cache.Metadata(r.hash,stage,error)==pointers[i],"metadata moved across cache rehash");
  const auto wrong=stage==ShaderStage::kVertex?ShaderStage::kPixel:ShaderStage::kVertex;
  Check(!cache.Metadata(r.hash,wrong,error)&&!error.empty(),"cached shader accepted wrong stage");
 }
 const auto& r=archive.records().front();const auto stage=ShaderStage(uint32_t(r.stage));
 uintptr_t checksum=0;
 for(unsigned trial=0;trial<7;++trial)for(bool borrow:{false,true}){
  const auto begin=std::chrono::steady_clock::now();allocations=0;count_allocations=true;
  for(unsigned i=0;i<100000;++i){
   if(borrow){auto* m=cache.Metadata(r.hash,stage,error);checksum^=uintptr_t(m);}
   else {auto m=cache.Lookup(r.hash,stage,error);checksum^=uintptr_t(m->name.data());}
  }
  count_allocations=false;const auto end=std::chrono::steady_clock::now();const auto count=allocations.load();
  if(borrow)Check(count==0,"immutable metadata allocated in the draw hot path");
  std::printf("metadata_benchmark borrowed=%u trial=%u calls=100000 ns=%lld allocations=%zu checksum=%llu\n",borrow,trial,(long long)std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count(),count,(unsigned long long)checksum);
 }
 cache.Clear();Check(!cache.Metadata(r.hash,stage,error),"Clear left stale archive metadata");
 Check(cache.InitializeFile(argv[1],error),error);Check(cache.Metadata(r.hash,stage,error)!=nullptr,error);
 std::printf("metal_shader_metadata=passed entries=%zu checks=%zu\n",pointers.size(),checks);return 0;
}catch(const std::exception& e){count_allocations=false;std::fprintf(stderr,"Shader metadata: %s\n",e.what());return 1;}}}
