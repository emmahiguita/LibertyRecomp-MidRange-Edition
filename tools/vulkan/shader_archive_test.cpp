#include "graphics/gta4_native/temporal/shader_archive.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

using rex::graphics::gta4_native::temporal::ShaderArchive;
using rex::graphics::gta4_native::temporal::ShaderStage;
namespace {
size_t checks=0;
void Check(bool value,const char* message) {
  ++checks;
  if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
// Archive parsing is independent of driver validation. Each payload is a
// distinct SPIR-V header so lookup can prove stage/override/late separation.
std::vector<uint32_t> Fixture() {
  return {1414943308u,827346245u,1,3,
          0,0,0,19,5,
          42,0,1,24,5,
          42,0,6,29,5,
          0x07230203u,0x00010000u,11,1,0,
          0x07230203u,0x00010000u,22,1,0,
          0x07230203u,0x00010000u,33,1,0};
}
bool Load(ShaderArchive& archive,const std::vector<uint32_t>& words,std::string& error) {
  std::vector<uint8_t> bytes;
  for(uint32_t word:words)
    for(unsigned shift:{0u,8u,16u,24u})bytes.push_back(uint8_t(word>>shift));
  return archive.Load(bytes,error);
}
}  // namespace

int main(int argc,char** argv) {
  ShaderArchive archive;
  std::string error;
  const auto original=Fixture();
  Check(Load(archive,original,error),"valid archive rejected");
  Check(error.empty(),"successful archive load retained an error");
  Check(archive.size()==3,"archive count");
  Check(archive.Find(0,ShaderStage::kPixel,false,false)[2]==11,"depth-only lookup");
  Check(archive.Find(42,ShaderStage::kVertex,false,false)[2]==22,"stock vertex lookup");
  Check(archive.Find(42,ShaderStage::kPixel,true,true)[2]==33,"override late lookup");
  Check(archive.Find(42,ShaderStage::kPixel,false,false).empty(),"stage mismatch matched");
  Check(archive.Find(42,ShaderStage::kPixel,true,false).empty(),"early/late mismatch matched");
  Check(archive.Find(42,ShaderStage::kVertex,true,false).empty(),"stock/override mismatch matched");
  Check(archive.Find(99,ShaderStage::kVertex,false,false).empty(),"unknown shader matched");
  Check(archive.Find(42,static_cast<ShaderStage>(7),false,false).empty(),"unknown stage matched");
  const auto rejected=[&](std::vector<uint32_t> words,const char* reason) {
    Check(!Load(archive,words,error),reason);
    Check(!error.empty(),"rejection omitted its diagnostic");
    Check(archive.size()==3&&archive.Find(42,ShaderStage::kVertex,false,false)[2]==22,
          "failed replacement damaged the last valid archive");
  };
  rejected({},"empty archive accepted");
  for(size_t word:{0u,1u,2u}) {
    auto data=original;data[word]=0;rejected(data,"bad magic/version accepted");
  }
  for(uint32_t count:{0u,0xFFFFFFFFu}) {
    auto data=original;data[3]=count;rejected(data,"invalid count accepted");
  }
  for(uint32_t flags:{8u,5u}) {
    auto data=original;data[11]=flags;rejected(data,"invalid record flags accepted");
  }
  {auto data=original;data[12]=23;rejected(data,"overlapping payload accepted");}
  {auto data=original;data[12]=25;rejected(data,"payload gap accepted");}
  {auto data=original;data[13]=0xFFFFFFFFu;rejected(data,"overflowing payload accepted");}
  {auto data=original;data[13]=4;rejected(data,"truncated SPIR-V header accepted");}
  {auto data=original;data[24]=0;rejected(data,"bad SPIR-V magic accepted");}
  {auto data=original;data[16]=1;rejected(data,"duplicate identity accepted");}
  {auto data=original;data[14]=1;rejected(data,"unsorted identity accepted");}
  {auto data=original;data.pop_back();rejected(data,"truncated archive accepted");}
  {auto data=original;data.push_back(0);rejected(data,"trailing data accepted");}
  if(argc==2) {
    Check(archive.Load(argv[1],error),error.c_str());
    Check(!archive.Find(0,ShaderStage::kPixel,false,false).empty(),"real archive omits depth-only producer");
    std::printf("published_temporal_archive_records=%zu\n",archive.size());
  } else Check(argc==1,"usage: shader-archive-test [temporal.bin]");
  std::printf("vulkan_temporal_shader_archive_checks=%zu passed\n",checks);
}
