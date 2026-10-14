#include "shader_archive.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <tuple>

namespace rex::graphics::gta4_native::temporal {
namespace {
constexpr size_t kMaximumBytes=256u*1024u*1024u;
constexpr size_t kMaximumRecords=16384;
constexpr std::array<uint32_t,2> kMagic{1414943308u,827346245u};
constexpr size_t kHeaderWords=4,kRecordWords=5;
uint32_t NativeWord(uint32_t value) {
  if constexpr(std::endian::native==std::endian::big)return std::byteswap(value);
  return value;
}
}  // namespace
bool ShaderArchive::Load(const std::filesystem::path& path,std::string& error) {
  std::ifstream input(path,std::ios::binary|std::ios::ate);
  if(!input){error="Vulkan temporal shader archive could not be opened";return false;}
  const auto bytes=input.tellg();
  if(bytes<0||uint64_t(bytes)>kMaximumBytes){error="Vulkan temporal shader archive exceeds its bound";return false;}
  std::vector<uint8_t> data(static_cast<size_t>(bytes));
  input.seekg(0);
  if(!input.read(reinterpret_cast<char*>(data.data()),bytes)){
    error="Vulkan temporal shader archive read failed";return false;
  }
  return Load(data,error);
}
bool ShaderArchive::Load(std::span<const uint8_t> bytes,std::string& error) {
  error.clear();
  if(bytes.size()<kHeaderWords*sizeof(uint32_t)||bytes.size()>kMaximumBytes||
      bytes.size()%sizeof(uint32_t)){
    error="Invalid Vulkan temporal shader archive extent";return false;
  }
  std::vector<uint32_t> words(bytes.size()/sizeof(uint32_t));
  std::memcpy(words.data(),bytes.data(),bytes.size());
  for(auto& word:words)word=NativeWord(word);
  if(words[0]!=kMagic[0]||words[1]!=kMagic[1]||words[2]!=1||!words[3]||words[3]>kMaximumRecords||
      words[3]>(words.size()-kHeaderWords)/kRecordWords){
    error="Invalid Vulkan temporal shader archive header";return false;
  }
  const size_t payload=kHeaderWords+size_t(words[3])*kRecordWords;
  std::vector<Record> records;records.reserve(words[3]);
  size_t next=payload;
  for(size_t i=0;i<words[3];++i){
    const auto* row=words.data()+kHeaderWords+i*kRecordWords;
    Record record{uint64_t(row[0])|(uint64_t(row[1])<<32),row[2],row[3],row[4]};
    if(record.flags>7||((record.flags&1)&&(record.flags&4))||record.offset!=next||
        record.offset>words.size()||record.count>words.size()-record.offset||record.count<5||
        words[record.offset]!=0x07230203u||
        (!records.empty()&&std::tie(record.hash,record.flags)<=
                           std::tie(records.back().hash,records.back().flags))){
      error="Invalid Vulkan temporal shader record or SPIR-V range";return false;
    }
    next+=record.count;records.push_back(record);
  }
  if(next!=words.size()){error="Trailing Vulkan temporal shader archive payload";return false;}
  words_=std::move(words);records_=std::move(records);return true;
}
std::span<const uint32_t> ShaderArchive::Find(uint64_t hash,ShaderStage stage,bool overridden,
                                            bool late) const {
  if(stage!=ShaderStage::kPixel&&stage!=ShaderStage::kVertex)return {};
  const uint32_t flags=uint32_t(stage)|(overridden?2u:0u)|(late?4u:0u);
  const auto key=std::make_pair(hash,flags);
  const auto found=std::lower_bound(records_.begin(),records_.end(),key,
      [](const Record& record,const auto& wanted){return std::make_pair(record.hash,record.flags)<wanted;});
  if(found==records_.end()||found->hash!=hash||found->flags!=flags)return {};
  return {words_.data()+found->offset,found->count};
}
}  // namespace rex::graphics::gta4_native::temporal
