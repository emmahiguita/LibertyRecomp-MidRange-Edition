#include <rex/graphics/pipeline/texture/conversion.h>

static void ExerciseTextureEndianBounds() {
  using namespace rex::graphics;
  size_t cases=0;
  for(auto endian:{xenos::Endian::kNone,xenos::Endian::k8in16,xenos::Endian::k8in32,xenos::Endian::k16in32})
    for(size_t bytes:{0u,4u,8u,12u,16u,20u,32u,64u}) for(size_t offset:{1u,3u,16u}) {
      std::array<uint8_t,384> input{},output{},expected{};
      for(size_t i=0;i<input.size();++i) input[i]=uint8_t(i*37+11);
      output.fill(0xA5);expected.fill(0xA5);
      const size_t mask=endian==xenos::Endian::k8in16?1:endian==xenos::Endian::k8in32?3:endian==xenos::Endian::k16in32?2:0;
      // Independent byte permutation, not the implementation under test.
      for(size_t i=0;i<bytes;++i) expected[offset+i]=input[offset+(i^mask)];
      texture_conversion::CopySwapBlock(endian,output.data()+offset,input.data()+offset,bytes);
      if(output!=expected) {
        auto difference=std::mismatch(output.begin(),output.end(),expected.begin());
        std::fprintf(stderr,"Texture endian bounds: mode=%u bytes=%zu offset=%zu changed-byte=%zu expected=%u actual=%u\n",
            unsigned(endian),bytes,offset,size_t(difference.first-output.begin()),unsigned(*difference.second),unsigned(*difference.first));
        Check(false,"Texture byte-order conversion wrote outside its requested block");
      }
      ++cases;
    }
  std::printf("texture_endian_bounds=passed cases=%zu\n",cases);
}
