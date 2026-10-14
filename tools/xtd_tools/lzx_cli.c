/*
 * LZX CLI Tool - Command-line LZX compression/decompression using xcompress
 * DLLs Compile: i686-w64-mingw32-gcc -o lzx_cli.exe lzx_cli.c -L.
 * -lxcompress_cpp Usage: wine lzx_cli.exe decompress input.lzx output.bin wine
 * lzx_cli.exe compress input.bin output.lzx
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// Function pointers for DLL imports
typedef int(__cdecl *LZXinit_t)(int window);
typedef int(__cdecl *LZXdecompress_t)(const char *inData, int inlen,
                                      char *outData, int outlen);

// xcompress.dll function pointers
typedef HRESULT(__stdcall *XMemCreateCompressionContext_t)(int CodecType,
                                                           void *pCodecParams,
                                                           int Flags,
                                                           int *pContext);
typedef void(__stdcall *XMemDestroyCompressionContext_t)(int Context);
typedef HRESULT(__stdcall *XMemResetCompressionContext_t)(int Context);
typedef HRESULT(__stdcall *XMemCompress_t)(int Context, void *pDest,
                                           int *pDestSize, void *pSrc,
                                           int SrcSize);

#define XMEMCODEC_LZX 1
#define XMEMCOMPRESS_STREAM 0x00000001

static HMODULE hXCompressCpp = NULL;
static HMODULE hXCompress = NULL;
static LZXinit_t pfnLZXinit = NULL;
static LZXdecompress_t pfnLZXdecompress = NULL;
static XMemCreateCompressionContext_t pfnXMemCreateCompressionContext = NULL;
static XMemDestroyCompressionContext_t pfnXMemDestroyCompressionContext = NULL;
static XMemResetCompressionContext_t pfnXMemResetCompressionContext = NULL;
static XMemCompress_t pfnXMemCompress = NULL;

int load_dlls(void) {
  // Load xcompress_cpp.dll for decompression
  hXCompressCpp = LoadLibraryA("xcompress_cpp.dll");
  if (!hXCompressCpp) {
    fprintf(stderr, "Failed to load xcompress_cpp.dll (error %lu)\n",
            GetLastError());
    return 0;
  }

  pfnLZXinit = (LZXinit_t)GetProcAddress(hXCompressCpp, "LZXinit");
  pfnLZXdecompress =
      (LZXdecompress_t)GetProcAddress(hXCompressCpp, "LZXdecompress");

  if (!pfnLZXinit || !pfnLZXdecompress) {
    fprintf(stderr, "Failed to get xcompress_cpp.dll functions\n");
    return 0;
  }

  // Load xcompress.dll for compression
  hXCompress = LoadLibraryA("xcompress.dll");
  if (!hXCompress) {
    fprintf(stderr,
            "Warning: xcompress.dll not found, compression unavailable\n");
  } else {
    pfnXMemCreateCompressionContext =
        (XMemCreateCompressionContext_t)GetProcAddress(
            hXCompress, "XMemCreateCompressionContext");
    pfnXMemDestroyCompressionContext =
        (XMemDestroyCompressionContext_t)GetProcAddress(
            hXCompress, "XMemDestroyCompressionContext");
    pfnXMemResetCompressionContext =
        (XMemResetCompressionContext_t)GetProcAddress(
            hXCompress, "XMemResetCompressionContext");
    pfnXMemCompress =
        (XMemCompress_t)GetProcAddress(hXCompress, "XMemCompress");
  }

  return 1;
}

void unload_dlls(void) {
  if (hXCompressCpp)
    FreeLibrary(hXCompressCpp);
  if (hXCompress)
    FreeLibrary(hXCompress);
}

// Read LZX block size from input
// Returns: 0 = EOF, -1 = error
int read_block_header(FILE *fp, unsigned short *uncompressed,
                      unsigned short *compressed) {
  unsigned char b0, b1, b2, b3, b4;

  if (fread(&b0, 1, 1, fp) != 1)
    return 0; // EOF

  if (b0 == 0xFF) {
    // 5-byte header
    if (fread(&b1, 1, 1, fp) != 1)
      return -1;
    if (fread(&b2, 1, 1, fp) != 1)
      return -1;
    if (fread(&b3, 1, 1, fp) != 1)
      return -1;
    if (fread(&b4, 1, 1, fp) != 1)
      return -1;
    *uncompressed = (b1 << 8) | b2;
    *compressed = (b3 << 8) | b4;
  } else {
    // 2-byte header
    if (fread(&b1, 1, 1, fp) != 1)
      return -1;
    *uncompressed = 0x8000;
    *compressed = (b0 << 8) | b1;
  }

  return 1;
}

// Write LZX block header
void write_block_header(FILE *fp, unsigned short uncompressed,
                        unsigned short compressed) {
  if (uncompressed == 0x8000) {
    // 2-byte header
    unsigned char b0 = (compressed >> 8) & 0xFF;
    unsigned char b1 = compressed & 0xFF;
    fwrite(&b0, 1, 1, fp);
    fwrite(&b1, 1, 1, fp);
  } else {
    // 5-byte header
    unsigned char b0 = 0xFF;
    unsigned char b1 = (uncompressed >> 8) & 0xFF;
    unsigned char b2 = uncompressed & 0xFF;
    unsigned char b3 = (compressed >> 8) & 0xFF;
    unsigned char b4 = compressed & 0xFF;
    fwrite(&b0, 1, 1, fp);
    fwrite(&b1, 1, 1, fp);
    fwrite(&b2, 1, 1, fp);
    fwrite(&b3, 1, 1, fp);
    fwrite(&b4, 1, 1, fp);
  }
}

int decompress_file(const char *infile, const char *outfile) {
  FILE *fin = fopen(infile, "rb");
  if (!fin) {
    fprintf(stderr, "Cannot open input file: %s\n", infile);
    return 1;
  }

  FILE *fout = fopen(outfile, "wb");
  if (!fout) {
    fclose(fin);
    fprintf(stderr, "Cannot create output file: %s\n", outfile);
    return 1;
  }

  // Initialize LZX with window size 17
  pfnLZXinit(17);

  unsigned short uncompressed, compressed;
  char *inbuf = NULL;
  char *outbuf = NULL;
  int result;
  long total_out = 0;
  int block_count = 0;

  while ((result = read_block_header(fin, &uncompressed, &compressed)) == 1) {
    inbuf = (char *)realloc(inbuf, compressed);
    outbuf = (char *)realloc(outbuf, uncompressed);

    if (fread(inbuf, 1, compressed, fin) != compressed) {
      fprintf(stderr, "Error reading block %d\n", block_count);
      break;
    }

    pfnLZXdecompress(inbuf, compressed, outbuf, uncompressed);
    fwrite(outbuf, 1, uncompressed, fout);

    total_out += uncompressed;
    block_count++;
  }

  if (inbuf)
    free(inbuf);
  if (outbuf)
    free(outbuf);
  fclose(fin);
  fclose(fout);

  printf("Decompressed %d blocks, %ld bytes total\n", block_count, total_out);
  return 0;
}

int compress_file(const char *infile, const char *outfile) {
  if (!pfnXMemCompress) {
    fprintf(stderr, "Compression not available (xcompress.dll not loaded)\n");
    return 1;
  }

  FILE *fin = fopen(infile, "rb");
  if (!fin) {
    fprintf(stderr, "Cannot open input file: %s\n", infile);
    return 1;
  }

  // Get input file size
  fseek(fin, 0, SEEK_END);
  long insize = ftell(fin);
  fseek(fin, 0, SEEK_SET);

  // Read all input
  char *inbuf = (char *)malloc(insize);
  if (fread(inbuf, 1, insize, fin) != insize) {
    fclose(fin);
    free(inbuf);
    fprintf(stderr, "Error reading input file\n");
    return 1;
  }
  fclose(fin);

  FILE *fout = fopen(outfile, "wb");
  if (!fout) {
    free(inbuf);
    fprintf(stderr, "Cannot create output file: %s\n", outfile);
    return 1;
  }

  // Create compression context
  int context = 0;
  HRESULT hr = pfnXMemCreateCompressionContext(XMEMCODEC_LZX, NULL,
                                               XMEMCOMPRESS_STREAM, &context);
  if (FAILED(hr)) {
    free(inbuf);
    fclose(fout);
    fprintf(stderr, "Failed to create compression context (0x%08lX)\n", hr);
    return 1;
  }

  pfnXMemResetCompressionContext(context);

  // Allocate output buffer (worst case: 2x input size)
  int outsize = insize * 2;
  char *outbuf = (char *)malloc(outsize);

  hr = pfnXMemCompress(context, outbuf, &outsize, inbuf, insize);
  if (FAILED(hr)) {
    free(inbuf);
    free(outbuf);
    fclose(fout);
    pfnXMemDestroyCompressionContext(context);
    fprintf(stderr, "Compression failed (0x%08lX)\n", hr);
    return 1;
  }

  fwrite(outbuf, 1, outsize, fout);

  free(inbuf);
  free(outbuf);
  fclose(fout);
  pfnXMemDestroyCompressionContext(context);

  printf("Compressed %ld bytes to %d bytes\n", insize, outsize);
  return 0;
}

void print_usage(const char *prog) {
  printf("LZX CLI Tool - Xbox 360 LZX Compression/Decompression\n\n");
  printf("Usage:\n");
  printf("  %s decompress <input.lzx> <output.bin>\n", prog);
  printf("  %s compress <input.bin> <output.lzx>\n", prog);
  printf("  %s analyze <input.lzx>\n", prog);
}

int analyze_file(const char *infile) {
  FILE *fin = fopen(infile, "rb");
  if (!fin) {
    fprintf(stderr, "Cannot open file: %s\n", infile);
    return 1;
  }

  fseek(fin, 0, SEEK_END);
  long size = ftell(fin);
  fseek(fin, 0, SEEK_SET);

  printf("File: %s\n", infile);
  printf("Size: %ld bytes\n", size);

  unsigned short uncompressed, compressed;
  int result;
  int block_count = 0;
  long total_uncompressed = 0;
  long total_compressed = 0;

  printf("\nBlock Analysis:\n");
  printf("  Block#  Compressed  Uncompressed\n");
  printf("  ------------------------------\n");

  while ((result = read_block_header(fin, &uncompressed, &compressed)) == 1) {
    if (block_count < 10) {
      printf("  %6d  %10u  %12u\n", block_count, compressed, uncompressed);
    } else if (block_count == 10) {
      printf("  ... (more blocks)\n");
    }

    // Skip block data
    fseek(fin, compressed, SEEK_CUR);

    total_uncompressed += uncompressed;
    total_compressed += compressed;
    block_count++;
  }

  fclose(fin);

  printf("  ------------------------------\n");
  printf("  Total: %d blocks\n", block_count);
  printf("  Compressed: %ld bytes\n", total_compressed);
  printf("  Uncompressed: %ld bytes\n", total_uncompressed);
  printf("  Ratio: %.2f%%\n",
         (double)total_compressed / total_uncompressed * 100.0);

  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  if (strcmp(argv[1], "analyze") == 0) {
    if (argc < 3) {
      print_usage(argv[0]);
      return 1;
    }
    return analyze_file(argv[2]);
  }

  if (!load_dlls()) {
    fprintf(stderr, "Failed to load required DLLs\n");
    return 1;
  }

  int result = 1;

  if (strcmp(argv[1], "decompress") == 0 && argc >= 4) {
    result = decompress_file(argv[2], argv[3]);
  } else if (strcmp(argv[1], "compress") == 0 && argc >= 4) {
    result = compress_file(argv[2], argv[3]);
  } else {
    print_usage(argv[0]);
  }

  unload_dlls();
  return result;
}
