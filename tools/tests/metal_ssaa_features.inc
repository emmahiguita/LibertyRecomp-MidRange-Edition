// Every frontend SSAA pixel factor, including non-integer axis ratios.
void TestSupersamplingFactors(Fixture& f) {
  constexpr uint32_t width=19,height=13;
  size_t tested=0;
  for(uint32_t factor=2;factor<=16;factor+=2) {
    const auto extent=CalculateSupersampledExtent(width,height,factor,16384);
    Check(bool(extent),"SSAA extent calculation rejected a frontend factor");
    const auto mode=ParseAntiAliasingMode("ssaa"+std::to_string(factor)+"x");
    Check(mode&&GetAntiAliasingRoute(*mode).supersampling_pixel_factor==factor,"SSAA menu factor mapping");
    std::vector<Pixel> pixels(size_t(extent->width)*extent->height);
    for(uint32_t y=0;y<extent->height;++y)for(uint32_t x=0;x<extent->width;++x) {
      const float check=((x+y)&1)?1:0; pixels[size_t(y)*extent->width+x]={check,check,check,1};
    }
    auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:extent->width height:extent->height mipmapped:NO];
    d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead;
    auto source=[f.context->device newTextureWithDescriptor:d];Check(source!=nil,"SSAA source allocation");
    [source replaceRegion:MTLRegionMake2D(0,0,extent->width,extent->height) mipmapLevel:0 withBytes:pixels.data() bytesPerRow:extent->width*sizeof(Pixel)];
    d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:width height:height mipmapped:NO];
    d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget;
    auto destination=[f.context->device newTextureWithDescriptor:d];Check(destination!=nil,"SSAA destination allocation");
    auto commands=[f.context->queue commandBuffer];
    Check(f.post.Record(commands,source,destination,*mode,"high",false,f.error),f.error);
    const auto output=f.Read(commands,destination);
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
      const double x0=double(x)*extent->width/width,x1=double(x+1)*extent->width/width;
      const double y0=double(y)*extent->height/height,y1=double(y+1)*extent->height/height;
      double light=0;
      for(uint32_t sy=uint32_t(std::floor(y0));sy<uint32_t(std::ceil(y1));++sy)
        for(uint32_t sx=uint32_t(std::floor(x0));sx<uint32_t(std::ceil(x1));++sx) {
          const double area=std::max(0.0,std::min(x1,double(sx+1))-std::max(x0,double(sx)))*
                            std::max(0.0,std::min(y1,double(sy+1))-std::max(y0,double(sy)));
          light+=((sx+sy)&1)*area;
        }
      light/=(x1-x0)*(y1-y0);
      const double encoded=light<=0.0031308?light*12.92:1.055*std::pow(light,1.0/2.4)-0.055;
      for(size_t c=0;c<3;++c)Check(std::abs(output[size_t(y)*width+x][c]-encoded)<0.003,"SSAA did not reconstruct the complete linear-light footprint");
    }
    ++tested;
  }
  std::printf("metal_ssaa_reconstruction=passed factors=%zu non_square=true fractional_footprints=true\n",tested);
}
