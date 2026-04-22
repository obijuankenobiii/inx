/**
 * @file miniz.h
 * @brief Public interface and types for miniz.
 */

#define MINIZ_EXPORT

#pragma once


























#if defined(__TINYC__) && (defined(__linux) || defined(__linux__))

#define MINIZ_NO_TIME
#endif

#include <stddef.h>

#if !defined(MINIZ_NO_TIME) && !defined(MINIZ_NO_ARCHIVE_APIS)
#include <time.h>
#endif

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__i386) || defined(__i486__) || \
    defined(__i486) || defined(i386) || defined(__ia64__) || defined(__x86_64__)

#define MINIZ_X86_OR_X64_CPU 1
#else
#define MINIZ_X86_OR_X64_CPU 0
#endif

#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) || MINIZ_X86_OR_X64_CPU

#define MINIZ_LITTLE_ENDIAN 1
#else
#define MINIZ_LITTLE_ENDIAN 0
#endif


#if !defined(MINIZ_USE_UNALIGNED_LOADS_AND_STORES)
#if MINIZ_X86_OR_X64_CPU

#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 1
#define MINIZ_UNALIGNED_USE_MEMCPY
#else
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 0
#endif
#endif

#if defined(_M_X64) || defined(_WIN64) || defined(__MINGW64__) || defined(_LP64) || defined(__LP64__) || \
    defined(__ia64__) || defined(__x86_64__)

#define MINIZ_HAS_64BIT_REGISTERS 1
#else
#define MINIZ_HAS_64BIT_REGISTERS 0
#endif

#ifdef __cplusplus
extern "C" {
#endif




typedef unsigned long mz_ulong;


MINIZ_EXPORT void mz_free(void* p);

#define MZ_ADLER32_INIT (1)

MINIZ_EXPORT mz_ulong mz_adler32(mz_ulong adler, const unsigned char* ptr, size_t buf_len);

#define MZ_CRC32_INIT (0)

MINIZ_EXPORT mz_ulong mz_crc32(mz_ulong crc, const unsigned char* ptr, size_t buf_len);


enum { MZ_DEFAULT_STRATEGY = 0, MZ_FILTERED = 1, MZ_HUFFMAN_ONLY = 2, MZ_RLE = 3, MZ_FIXED = 4 };


#define MZ_DEFLATED 8


typedef void* (*mz_alloc_func)(void* opaque, size_t items, size_t size);
typedef void (*mz_free_func)(void* opaque, void* address);
typedef void* (*mz_realloc_func)(void* opaque, void* address, size_t items, size_t size);


enum {
  MZ_NO_COMPRESSION = 0,
  MZ_BEST_SPEED = 1,
  MZ_BEST_COMPRESSION = 9,
  MZ_UBER_COMPRESSION = 10,
  MZ_DEFAULT_LEVEL = 6,
  MZ_DEFAULT_COMPRESSION = -1
};

#define MZ_VERSION "10.2.0"
#define MZ_VERNUM 0xA100
#define MZ_VER_MAJOR 10
#define MZ_VER_MINOR 2
#define MZ_VER_REVISION 0
#define MZ_VER_SUBREVISION 0

#ifndef MINIZ_NO_ZLIB_APIS


enum { MZ_NO_FLUSH = 0, MZ_PARTIAL_FLUSH = 1, MZ_SYNC_FLUSH = 2, MZ_FULL_FLUSH = 3, MZ_FINISH = 4, MZ_BLOCK = 5 };


enum {
  MZ_OK = 0,
  MZ_STREAM_END = 1,
  MZ_NEED_DICT = 2,
  MZ_ERRNO = -1,
  MZ_STREAM_ERROR = -2,
  MZ_DATA_ERROR = -3,
  MZ_MEM_ERROR = -4,
  MZ_BUF_ERROR = -5,
  MZ_VERSION_ERROR = -6,
  MZ_PARAM_ERROR = -10000
};


#define MZ_DEFAULT_WINDOW_BITS 15

struct mz_internal_state;


typedef struct mz_stream_s {
  const unsigned char* next_in; 
  unsigned int avail_in;        
  mz_ulong total_in;            

  unsigned char* next_out; 
  unsigned int avail_out;  
  mz_ulong total_out;      

  char* msg;                       
  struct mz_internal_state* state; 

  mz_alloc_func zalloc; 
  mz_free_func zfree;   
  void* opaque;         

  int data_type;     
  mz_ulong adler;    
  mz_ulong reserved; 
} mz_stream;

typedef mz_stream* mz_streamp;


MINIZ_EXPORT const char* mz_version(void);












MINIZ_EXPORT int mz_deflateInit(mz_streamp pStream, int level);






MINIZ_EXPORT int mz_deflateInit2(mz_streamp pStream, int level, int method, int window_bits, int mem_level,
                                 int strategy);


MINIZ_EXPORT int mz_deflateReset(mz_streamp pStream);











MINIZ_EXPORT int mz_deflate(mz_streamp pStream, int flush);





MINIZ_EXPORT int mz_deflateEnd(mz_streamp pStream);


MINIZ_EXPORT mz_ulong mz_deflateBound(mz_streamp pStream, mz_ulong source_len);



MINIZ_EXPORT int mz_compress(unsigned char* pDest, mz_ulong* pDest_len, const unsigned char* pSource,
                             mz_ulong source_len);
MINIZ_EXPORT int mz_compress2(unsigned char* pDest, mz_ulong* pDest_len, const unsigned char* pSource,
                              mz_ulong source_len, int level);


MINIZ_EXPORT mz_ulong mz_compressBound(mz_ulong source_len);


MINIZ_EXPORT int mz_inflateInit(mz_streamp pStream);



MINIZ_EXPORT int mz_inflateInit2(mz_streamp pStream, int window_bits);


MINIZ_EXPORT int mz_inflateReset(mz_streamp pStream);















MINIZ_EXPORT int mz_inflate(mz_streamp pStream, int flush);


MINIZ_EXPORT int mz_inflateEnd(mz_streamp pStream);



MINIZ_EXPORT int mz_uncompress(unsigned char* pDest, mz_ulong* pDest_len, const unsigned char* pSource,
                               mz_ulong source_len);
MINIZ_EXPORT int mz_uncompress2(unsigned char* pDest, mz_ulong* pDest_len, const unsigned char* pSource,
                                mz_ulong* pSource_len);


MINIZ_EXPORT const char* mz_error(int err);



#ifndef MINIZ_NO_ZLIB_COMPATIBLE_NAMES
typedef unsigned char Byte;
typedef unsigned int uInt;
typedef mz_ulong uLong;
typedef Byte Bytef;
typedef uInt uIntf;
typedef char charf;
typedef int intf;
typedef void* voidpf;
typedef uLong uLongf;
typedef void* voidp;
typedef void* const voidpc;
#define Z_NULL 0
#define Z_NO_FLUSH MZ_NO_FLUSH
#define Z_PARTIAL_FLUSH MZ_PARTIAL_FLUSH
#define Z_SYNC_FLUSH MZ_SYNC_FLUSH
#define Z_FULL_FLUSH MZ_FULL_FLUSH
#define Z_FINISH MZ_FINISH
#define Z_BLOCK MZ_BLOCK
#define Z_OK MZ_OK
#define Z_STREAM_END MZ_STREAM_END
#define Z_NEED_DICT MZ_NEED_DICT
#define Z_ERRNO MZ_ERRNO
#define Z_STREAM_ERROR MZ_STREAM_ERROR
#define Z_DATA_ERROR MZ_DATA_ERROR
#define Z_MEM_ERROR MZ_MEM_ERROR
#define Z_BUF_ERROR MZ_BUF_ERROR
#define Z_VERSION_ERROR MZ_VERSION_ERROR
#define Z_PARAM_ERROR MZ_PARAM_ERROR
#define Z_NO_COMPRESSION MZ_NO_COMPRESSION
#define Z_BEST_SPEED MZ_BEST_SPEED
#define Z_BEST_COMPRESSION MZ_BEST_COMPRESSION
#define Z_DEFAULT_COMPRESSION MZ_DEFAULT_COMPRESSION
#define Z_DEFAULT_STRATEGY MZ_DEFAULT_STRATEGY
#define Z_FILTERED MZ_FILTERED
#define Z_HUFFMAN_ONLY MZ_HUFFMAN_ONLY
#define Z_RLE MZ_RLE
#define Z_FIXED MZ_FIXED
#define Z_DEFLATED MZ_DEFLATED
#define Z_DEFAULT_WINDOW_BITS MZ_DEFAULT_WINDOW_BITS
#define alloc_func mz_alloc_func
#define free_func mz_free_func
#define internal_state mz_internal_state
#define z_stream mz_stream
#define deflateInit mz_deflateInit
#define deflateInit2 mz_deflateInit2
#define deflateReset mz_deflateReset
#define deflate mz_deflate
#define deflateEnd mz_deflateEnd
#define deflateBound mz_deflateBound
#define compress mz_compress
#define compress2 mz_compress2
#define compressBound mz_compressBound
#define inflateInit mz_inflateInit
#define inflateInit2 mz_inflateInit2
#define inflateReset mz_inflateReset
#define inflate mz_inflate
#define inflateEnd mz_inflateEnd
#define uncompress mz_uncompress
#define uncompress2 mz_uncompress2
#define crc32 mz_crc32
#define adler32 mz_adler32
#define MAX_WBITS 15
#define MAX_MEM_LEVEL 9
#define zError mz_error
#define ZLIB_VERSION MZ_VERSION
#define ZLIB_VERNUM MZ_VERNUM
#define ZLIB_VER_MAJOR MZ_VER_MAJOR
#define ZLIB_VER_MINOR MZ_VER_MINOR
#define ZLIB_VER_REVISION MZ_VER_REVISION
#define ZLIB_VER_SUBREVISION MZ_VER_SUBREVISION
#define zlibVersion mz_version
#define zlib_version mz_version()
#endif 

#endif 

#ifdef __cplusplus
}
#endif

#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


typedef unsigned char mz_uint8;
typedef signed short mz_int16;
typedef unsigned short mz_uint16;
typedef unsigned int mz_uint32;
typedef unsigned int mz_uint;
typedef int64_t mz_int64;
typedef uint64_t mz_uint64;
typedef int mz_bool;

#define MZ_FALSE (0)
#define MZ_TRUE (1)


#ifdef _MSC_VER
#define MZ_MACRO_END while (0, 0)
#else
#define MZ_MACRO_END while (0)
#endif

#ifdef MINIZ_NO_STDIO
#define MZ_FILE void*
#else
#include <stdio.h>
#define MZ_FILE FILE
#endif 

#ifdef MINIZ_NO_TIME
typedef struct mz_dummy_time_t_tag {
  int m_dummy;
} mz_dummy_time_t;
#define MZ_TIME_T mz_dummy_time_t
#else
#define MZ_TIME_T time_t
#endif

#define MZ_ASSERT(x) assert(x)

#ifdef MINIZ_NO_MALLOC
#define MZ_MALLOC(x) NULL
#define MZ_FREE(x) (void)x, ((void)0)
#define MZ_REALLOC(p, x) NULL
#else
#define MZ_MALLOC(x) malloc(x)
#define MZ_FREE(x) free(x)
#define MZ_REALLOC(p, x) realloc(p, x)
#endif

#define MZ_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MZ_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MZ_CLEAR_OBJ(obj) memset(&(obj), 0, sizeof(obj))

#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES && MINIZ_LITTLE_ENDIAN
#define MZ_READ_LE16(p) *((const mz_uint16*)(p))
#define MZ_READ_LE32(p) *((const mz_uint32*)(p))
#else
#define MZ_READ_LE16(p) ((mz_uint32)(((const mz_uint8*)(p))[0]) | ((mz_uint32)(((const mz_uint8*)(p))[1]) << 8U))
#define MZ_READ_LE32(p)                                                                      \
  ((mz_uint32)(((const mz_uint8*)(p))[0]) | ((mz_uint32)(((const mz_uint8*)(p))[1]) << 8U) | \
   ((mz_uint32)(((const mz_uint8*)(p))[2]) << 16U) | ((mz_uint32)(((const mz_uint8*)(p))[3]) << 24U))
#endif

#define MZ_READ_LE64(p) \
  (((mz_uint64)MZ_READ_LE32(p)) | (((mz_uint64)MZ_READ_LE32((const mz_uint8*)(p) + sizeof(mz_uint32))) << 32U))

#ifdef _MSC_VER
#define MZ_FORCEINLINE __forceinline
#elif defined(__GNUC__)
#define MZ_FORCEINLINE __inline__ __attribute__((__always_inline__))
#else
#define MZ_FORCEINLINE inline
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern MINIZ_EXPORT void* miniz_def_alloc_func(void* opaque, size_t items, size_t size);
extern MINIZ_EXPORT void miniz_def_free_func(void* opaque, void* address);
extern MINIZ_EXPORT void* miniz_def_realloc_func(void* opaque, void* address, size_t items, size_t size);

#define MZ_UINT16_MAX (0xFFFFU)
#define MZ_UINT32_MAX (0xFFFFFFFFU)

#ifdef __cplusplus
}
#endif
#pragma once

#ifdef __cplusplus
extern "C" {
#endif



#define TDEFL_LESS_MEMORY 0



enum { TDEFL_HUFFMAN_ONLY = 0, TDEFL_DEFAULT_MAX_PROBES = 128, TDEFL_MAX_PROBES_MASK = 0xFFF };










enum {
  TDEFL_WRITE_ZLIB_HEADER = 0x01000,
  TDEFL_COMPUTE_ADLER32 = 0x02000,
  TDEFL_GREEDY_PARSING_FLAG = 0x04000,
  TDEFL_NONDETERMINISTIC_PARSING_FLAG = 0x08000,
  TDEFL_RLE_MATCHES = 0x10000,
  TDEFL_FILTER_MATCHES = 0x20000,
  TDEFL_FORCE_ALL_STATIC_BLOCKS = 0x40000,
  TDEFL_FORCE_ALL_RAW_BLOCKS = 0x80000
};










MINIZ_EXPORT void* tdefl_compress_mem_to_heap(const void* pSrc_buf, size_t src_buf_len, size_t* pOut_len, int flags);



MINIZ_EXPORT size_t tdefl_compress_mem_to_mem(void* pOut_buf, size_t out_buf_len, const void* pSrc_buf,
                                              size_t src_buf_len, int flags);











MINIZ_EXPORT void* tdefl_write_image_to_png_file_in_memory_ex(const void* pImage, int w, int h, int num_chans,
                                                              size_t* pLen_out, mz_uint level, mz_bool flip);
MINIZ_EXPORT void* tdefl_write_image_to_png_file_in_memory(const void* pImage, int w, int h, int num_chans,
                                                           size_t* pLen_out);


typedef mz_bool (*tdefl_put_buf_func_ptr)(const void* pBuf, int len, void* pUser);


MINIZ_EXPORT mz_bool tdefl_compress_mem_to_output(const void* pBuf, size_t buf_len,
                                                  tdefl_put_buf_func_ptr pPut_buf_func, void* pPut_buf_user, int flags);

enum {
  TDEFL_MAX_HUFF_TABLES = 3,
  TDEFL_MAX_HUFF_SYMBOLS_0 = 288,
  TDEFL_MAX_HUFF_SYMBOLS_1 = 32,
  TDEFL_MAX_HUFF_SYMBOLS_2 = 19,
  TDEFL_LZ_DICT_SIZE = 32768,
  TDEFL_LZ_DICT_SIZE_MASK = TDEFL_LZ_DICT_SIZE - 1,
  TDEFL_MIN_MATCH_LEN = 3,
  TDEFL_MAX_MATCH_LEN = 258
};


#if TDEFL_LESS_MEMORY
enum {
  TDEFL_LZ_CODE_BUF_SIZE = 24 * 1024,
  TDEFL_OUT_BUF_SIZE = (TDEFL_LZ_CODE_BUF_SIZE * 13) / 10,
  TDEFL_MAX_HUFF_SYMBOLS = 288,
  TDEFL_LZ_HASH_BITS = 12,
  TDEFL_LEVEL1_HASH_SIZE_MASK = 4095,
  TDEFL_LZ_HASH_SHIFT = (TDEFL_LZ_HASH_BITS + 2) / 3,
  TDEFL_LZ_HASH_SIZE = 1 << TDEFL_LZ_HASH_BITS
};
#else
enum {
  TDEFL_LZ_CODE_BUF_SIZE = 64 * 1024,
  TDEFL_OUT_BUF_SIZE = (TDEFL_LZ_CODE_BUF_SIZE * 13) / 10,
  TDEFL_MAX_HUFF_SYMBOLS = 288,
  TDEFL_LZ_HASH_BITS = 15,
  TDEFL_LEVEL1_HASH_SIZE_MASK = 4095,
  TDEFL_LZ_HASH_SHIFT = (TDEFL_LZ_HASH_BITS + 2) / 3,
  TDEFL_LZ_HASH_SIZE = 1 << TDEFL_LZ_HASH_BITS
};
#endif


typedef enum {
  TDEFL_STATUS_BAD_PARAM = -2,
  TDEFL_STATUS_PUT_BUF_FAILED = -1,
  TDEFL_STATUS_OKAY = 0,
  TDEFL_STATUS_DONE = 1
} tdefl_status;


typedef enum { TDEFL_NO_FLUSH = 0, TDEFL_SYNC_FLUSH = 2, TDEFL_FULL_FLUSH = 3, TDEFL_FINISH = 4 } tdefl_flush;


typedef struct {
  tdefl_put_buf_func_ptr m_pPut_buf_func;
  void* m_pPut_buf_user;
  mz_uint m_flags, m_max_probes[2];
  int m_greedy_parsing;
  mz_uint m_adler32, m_lookahead_pos, m_lookahead_size, m_dict_size;
  mz_uint8 *m_pLZ_code_buf, *m_pLZ_flags, *m_pOutput_buf, *m_pOutput_buf_end;
  mz_uint m_num_flags_left, m_total_lz_bytes, m_lz_code_buf_dict_pos, m_bits_in, m_bit_buffer;
  mz_uint m_saved_match_dist, m_saved_match_len, m_saved_lit, m_output_flush_ofs, m_output_flush_remaining, m_finished,
      m_block_index, m_wants_to_finish;
  tdefl_status m_prev_return_status;
  const void* m_pIn_buf;
  void* m_pOut_buf;
  size_t *m_pIn_buf_size, *m_pOut_buf_size;
  tdefl_flush m_flush;
  const mz_uint8* m_pSrc;
  size_t m_src_buf_left, m_out_buf_ofs;
  mz_uint8 m_dict[TDEFL_LZ_DICT_SIZE + TDEFL_MAX_MATCH_LEN - 1];
  mz_uint16 m_huff_count[TDEFL_MAX_HUFF_TABLES][TDEFL_MAX_HUFF_SYMBOLS];
  mz_uint16 m_huff_codes[TDEFL_MAX_HUFF_TABLES][TDEFL_MAX_HUFF_SYMBOLS];
  mz_uint8 m_huff_code_sizes[TDEFL_MAX_HUFF_TABLES][TDEFL_MAX_HUFF_SYMBOLS];
  mz_uint8 m_lz_code_buf[TDEFL_LZ_CODE_BUF_SIZE];
  mz_uint16 m_next[TDEFL_LZ_DICT_SIZE];
  mz_uint16 m_hash[TDEFL_LZ_HASH_SIZE];
  mz_uint8 m_output_buf[TDEFL_OUT_BUF_SIZE];
} tdefl_compressor;






MINIZ_EXPORT tdefl_status tdefl_init(tdefl_compressor* d, tdefl_put_buf_func_ptr pPut_buf_func, void* pPut_buf_user,
                                     int flags);


MINIZ_EXPORT tdefl_status tdefl_compress(tdefl_compressor* d, const void* pIn_buf, size_t* pIn_buf_size, void* pOut_buf,
                                         size_t* pOut_buf_size, tdefl_flush flush);



MINIZ_EXPORT tdefl_status tdefl_compress_buffer(tdefl_compressor* d, const void* pIn_buf, size_t in_buf_size,
                                                tdefl_flush flush);

MINIZ_EXPORT tdefl_status tdefl_get_prev_return_status(tdefl_compressor* d);
MINIZ_EXPORT mz_uint32 tdefl_get_adler32(tdefl_compressor* d);





MINIZ_EXPORT mz_uint tdefl_create_comp_flags_from_zip_params(int level, int window_bits, int strategy);

#ifndef MINIZ_NO_MALLOC



MINIZ_EXPORT tdefl_compressor* tdefl_compressor_alloc(void);
MINIZ_EXPORT void tdefl_compressor_free(tdefl_compressor* pComp);
#endif

#ifdef __cplusplus
}
#endif
#pragma once



#ifdef __cplusplus
extern "C" {
#endif





enum {
  TINFL_FLAG_PARSE_ZLIB_HEADER = 1,
  TINFL_FLAG_HAS_MORE_INPUT = 2,
  TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF = 4,
  TINFL_FLAG_COMPUTE_ADLER32 = 8
};









MINIZ_EXPORT void* tinfl_decompress_mem_to_heap(const void* pSrc_buf, size_t src_buf_len, size_t* pOut_len, int flags);



#define TINFL_DECOMPRESS_MEM_TO_MEM_FAILED ((size_t)(-1))
MINIZ_EXPORT size_t tinfl_decompress_mem_to_mem(void* pOut_buf, size_t out_buf_len, const void* pSrc_buf,
                                                size_t src_buf_len, int flags);



typedef int (*tinfl_put_buf_func_ptr)(const void* pBuf, int len, void* pUser);
MINIZ_EXPORT int tinfl_decompress_mem_to_callback(const void* pIn_buf, size_t* pIn_buf_size,
                                                  tinfl_put_buf_func_ptr pPut_buf_func, void* pPut_buf_user, int flags);

struct tinfl_decompressor_tag;
typedef struct tinfl_decompressor_tag tinfl_decompressor;

#ifndef MINIZ_NO_MALLOC



MINIZ_EXPORT tinfl_decompressor* tinfl_decompressor_alloc(void);
MINIZ_EXPORT void tinfl_decompressor_free(tinfl_decompressor* pDecomp);
#endif


#define TINFL_LZ_DICT_SIZE 32768


typedef enum {
  
  
  
  TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS = -4,

  
  TINFL_STATUS_BAD_PARAM = -3,

  
  TINFL_STATUS_ADLER32_MISMATCH = -2,

  
  TINFL_STATUS_FAILED = -1,

  

  
  
  TINFL_STATUS_DONE = 0,

  
  
  
  TINFL_STATUS_NEEDS_MORE_INPUT = 1,

  
  
  
  
  TINFL_STATUS_HAS_MORE_OUTPUT = 2
} tinfl_status;


#define tinfl_init(r) \
  do {                \
    (r)->m_state = 0; \
  }                   \
  MZ_MACRO_END
#define tinfl_get_adler32(r) (r)->m_check_adler32



MINIZ_EXPORT tinfl_status tinfl_decompress(tinfl_decompressor* r, const mz_uint8* pIn_buf_next, size_t* pIn_buf_size,
                                           mz_uint8* pOut_buf_start, mz_uint8* pOut_buf_next, size_t* pOut_buf_size,
                                           const mz_uint32 decomp_flags);


enum {
  TINFL_MAX_HUFF_TABLES = 3,
  TINFL_MAX_HUFF_SYMBOLS_0 = 288,
  TINFL_MAX_HUFF_SYMBOLS_1 = 32,
  TINFL_MAX_HUFF_SYMBOLS_2 = 19,
  TINFL_FAST_LOOKUP_BITS = 10,
  TINFL_FAST_LOOKUP_SIZE = 1 << TINFL_FAST_LOOKUP_BITS
};

typedef struct {
  mz_uint8 m_code_size[TINFL_MAX_HUFF_SYMBOLS_0];
  mz_int16 m_look_up[TINFL_FAST_LOOKUP_SIZE], m_tree[TINFL_MAX_HUFF_SYMBOLS_0 * 2];
} tinfl_huff_table;

#if MINIZ_HAS_64BIT_REGISTERS
#define TINFL_USE_64BIT_BITBUF 1
#else
#define TINFL_USE_64BIT_BITBUF 0
#endif

#if TINFL_USE_64BIT_BITBUF
typedef mz_uint64 tinfl_bit_buf_t;
#define TINFL_BITBUF_SIZE (64)
#else
typedef mz_uint32 tinfl_bit_buf_t;
#define TINFL_BITBUF_SIZE (32)
#endif

struct tinfl_decompressor_tag {
  mz_uint32 m_state, m_num_bits, m_zhdr0, m_zhdr1, m_z_adler32, m_final, m_type, m_check_adler32, m_dist, m_counter,
      m_num_extra, m_table_sizes[TINFL_MAX_HUFF_TABLES];
  tinfl_bit_buf_t m_bit_buf;
  size_t m_dist_from_out_buf_start;
  tinfl_huff_table m_tables[TINFL_MAX_HUFF_TABLES];
  mz_uint8 m_raw_header[4], m_len_codes[TINFL_MAX_HUFF_SYMBOLS_0 + TINFL_MAX_HUFF_SYMBOLS_1 + 137];
};

#ifdef __cplusplus
}
#endif

#pragma once



#ifndef MINIZ_NO_ARCHIVE_APIS

#ifdef __cplusplus
extern "C" {
#endif

enum {
  
  MZ_ZIP_MAX_IO_BUF_SIZE = 64 * 1024,
  MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE = 512,
  MZ_ZIP_MAX_ARCHIVE_FILE_COMMENT_SIZE = 512
};

typedef struct {
  
  mz_uint32 m_file_index;

  
  mz_uint64 m_central_dir_ofs;

  
  mz_uint16 m_version_made_by;
  mz_uint16 m_version_needed;
  mz_uint16 m_bit_flag;
  mz_uint16 m_method;

#ifndef MINIZ_NO_TIME
  MZ_TIME_T m_time;
#endif

  
  mz_uint32 m_crc32;

  
  mz_uint64 m_comp_size;

  
  mz_uint64 m_uncomp_size;

  
  mz_uint16 m_internal_attr;
  mz_uint32 m_external_attr;

  
  mz_uint64 m_local_header_ofs;

  
  mz_uint32 m_comment_size;

  
  mz_bool m_is_directory;

  
  mz_bool m_is_encrypted;

  
  mz_bool m_is_supported;

  
  
  char m_filename[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];

  
  
  char m_comment[MZ_ZIP_MAX_ARCHIVE_FILE_COMMENT_SIZE];

} mz_zip_archive_file_stat;

typedef size_t (*mz_file_read_func)(void* pOpaque, mz_uint64 file_ofs, void* pBuf, size_t n);
typedef size_t (*mz_file_write_func)(void* pOpaque, mz_uint64 file_ofs, const void* pBuf, size_t n);
typedef mz_bool (*mz_file_needs_keepalive)(void* pOpaque);

struct mz_zip_internal_state_tag;
typedef struct mz_zip_internal_state_tag mz_zip_internal_state;

typedef enum {
  MZ_ZIP_MODE_INVALID = 0,
  MZ_ZIP_MODE_READING = 1,
  MZ_ZIP_MODE_WRITING = 2,
  MZ_ZIP_MODE_WRITING_HAS_BEEN_FINALIZED = 3
} mz_zip_mode;

typedef enum {
  MZ_ZIP_FLAG_CASE_SENSITIVE = 0x0100,
  MZ_ZIP_FLAG_IGNORE_PATH = 0x0200,
  MZ_ZIP_FLAG_COMPRESSED_DATA = 0x0400,
  MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY = 0x0800,
  MZ_ZIP_FLAG_VALIDATE_LOCATE_FILE_FLAG =
      0x1000, 
  MZ_ZIP_FLAG_VALIDATE_HEADERS_ONLY =
      0x2000, 
  MZ_ZIP_FLAG_WRITE_ZIP64 = 0x4000, 
  MZ_ZIP_FLAG_WRITE_ALLOW_READING = 0x8000,
  MZ_ZIP_FLAG_ASCII_FILENAME = 0x10000,
  
  MZ_ZIP_FLAG_WRITE_HEADER_SET_SIZE = 0x20000
} mz_zip_flags;

typedef enum {
  MZ_ZIP_TYPE_INVALID = 0,
  MZ_ZIP_TYPE_USER,
  MZ_ZIP_TYPE_MEMORY,
  MZ_ZIP_TYPE_HEAP,
  MZ_ZIP_TYPE_FILE,
  MZ_ZIP_TYPE_CFILE,
  MZ_ZIP_TOTAL_TYPES
} mz_zip_type;


typedef enum {
  MZ_ZIP_NO_ERROR = 0,
  MZ_ZIP_UNDEFINED_ERROR,
  MZ_ZIP_TOO_MANY_FILES,
  MZ_ZIP_FILE_TOO_LARGE,
  MZ_ZIP_UNSUPPORTED_METHOD,
  MZ_ZIP_UNSUPPORTED_ENCRYPTION,
  MZ_ZIP_UNSUPPORTED_FEATURE,
  MZ_ZIP_FAILED_FINDING_CENTRAL_DIR,
  MZ_ZIP_NOT_AN_ARCHIVE,
  MZ_ZIP_INVALID_HEADER_OR_CORRUPTED,
  MZ_ZIP_UNSUPPORTED_MULTIDISK,
  MZ_ZIP_DECOMPRESSION_FAILED,
  MZ_ZIP_COMPRESSION_FAILED,
  MZ_ZIP_UNEXPECTED_DECOMPRESSED_SIZE,
  MZ_ZIP_CRC_CHECK_FAILED,
  MZ_ZIP_UNSUPPORTED_CDIR_SIZE,
  MZ_ZIP_ALLOC_FAILED,
  MZ_ZIP_FILE_OPEN_FAILED,
  MZ_ZIP_FILE_CREATE_FAILED,
  MZ_ZIP_FILE_WRITE_FAILED,
  MZ_ZIP_FILE_READ_FAILED,
  MZ_ZIP_FILE_CLOSE_FAILED,
  MZ_ZIP_FILE_SEEK_FAILED,
  MZ_ZIP_FILE_STAT_FAILED,
  MZ_ZIP_INVALID_PARAMETER,
  MZ_ZIP_INVALID_FILENAME,
  MZ_ZIP_BUF_TOO_SMALL,
  MZ_ZIP_INTERNAL_ERROR,
  MZ_ZIP_FILE_NOT_FOUND,
  MZ_ZIP_ARCHIVE_TOO_LARGE,
  MZ_ZIP_VALIDATION_FAILED,
  MZ_ZIP_WRITE_CALLBACK_FAILED,
  MZ_ZIP_TOTAL_ERRORS
} mz_zip_error;

typedef struct {
  mz_uint64 m_archive_size;
  mz_uint64 m_central_directory_file_ofs;

  
  mz_uint32 m_total_files;
  mz_zip_mode m_zip_mode;
  mz_zip_type m_zip_type;
  mz_zip_error m_last_error;

  mz_uint64 m_file_offset_alignment;

  mz_alloc_func m_pAlloc;
  mz_free_func m_pFree;
  mz_realloc_func m_pRealloc;
  void* m_pAlloc_opaque;

  mz_file_read_func m_pRead;
  mz_file_write_func m_pWrite;
  mz_file_needs_keepalive m_pNeeds_keepalive;
  void* m_pIO_opaque;

  mz_zip_internal_state* m_pState;

} mz_zip_archive;

typedef struct {
  mz_zip_archive* pZip;
  mz_uint flags;

  int status;
#ifndef MINIZ_DISABLE_ZIP_READER_CRC32_CHECKS
  mz_uint file_crc32;
#endif
  mz_uint64 read_buf_size, read_buf_ofs, read_buf_avail, comp_remaining, out_buf_ofs, cur_file_ofs;
  mz_zip_archive_file_stat file_stat;
  void* pRead_buf;
  void* pWrite_buf;

  size_t out_blk_remain;

  tinfl_decompressor inflator;

} mz_zip_reader_extract_iter_state;





MINIZ_EXPORT mz_bool mz_zip_reader_init(mz_zip_archive* pZip, mz_uint64 size, mz_uint flags);

MINIZ_EXPORT mz_bool mz_zip_reader_init_mem(mz_zip_archive* pZip, const void* pMem, size_t size, mz_uint flags);

#ifndef MINIZ_NO_STDIO



MINIZ_EXPORT mz_bool mz_zip_reader_init_file(mz_zip_archive* pZip, const char* pFilename, mz_uint32 flags);
MINIZ_EXPORT mz_bool mz_zip_reader_init_file_v2(mz_zip_archive* pZip, const char* pFilename, mz_uint flags,
                                                mz_uint64 file_start_ofs, mz_uint64 archive_size);




MINIZ_EXPORT mz_bool mz_zip_reader_init_cfile(mz_zip_archive* pZip, MZ_FILE* pFile, mz_uint64 archive_size,
                                              mz_uint flags);
#endif


MINIZ_EXPORT mz_bool mz_zip_reader_end(mz_zip_archive* pZip);





MINIZ_EXPORT void mz_zip_zero_struct(mz_zip_archive* pZip);

MINIZ_EXPORT mz_zip_mode mz_zip_get_mode(mz_zip_archive* pZip);
MINIZ_EXPORT mz_zip_type mz_zip_get_type(mz_zip_archive* pZip);


MINIZ_EXPORT mz_uint mz_zip_reader_get_num_files(mz_zip_archive* pZip);

MINIZ_EXPORT mz_uint64 mz_zip_get_archive_size(mz_zip_archive* pZip);
MINIZ_EXPORT mz_uint64 mz_zip_get_archive_file_start_offset(mz_zip_archive* pZip);
MINIZ_EXPORT MZ_FILE* mz_zip_get_cfile(mz_zip_archive* pZip);


MINIZ_EXPORT size_t mz_zip_read_archive_data(mz_zip_archive* pZip, mz_uint64 file_ofs, void* pBuf, size_t n);



MINIZ_EXPORT mz_zip_error mz_zip_set_last_error(mz_zip_archive* pZip, mz_zip_error err_num);
MINIZ_EXPORT mz_zip_error mz_zip_peek_last_error(mz_zip_archive* pZip);
MINIZ_EXPORT mz_zip_error mz_zip_clear_last_error(mz_zip_archive* pZip);
MINIZ_EXPORT mz_zip_error mz_zip_get_last_error(mz_zip_archive* pZip);
MINIZ_EXPORT const char* mz_zip_get_error_string(mz_zip_error mz_err);


MINIZ_EXPORT mz_bool mz_zip_reader_is_file_a_directory(mz_zip_archive* pZip, mz_uint file_index);


MINIZ_EXPORT mz_bool mz_zip_reader_is_file_encrypted(mz_zip_archive* pZip, mz_uint file_index);


MINIZ_EXPORT mz_bool mz_zip_reader_is_file_supported(mz_zip_archive* pZip, mz_uint file_index);



MINIZ_EXPORT mz_uint mz_zip_reader_get_filename(mz_zip_archive* pZip, mz_uint file_index, char* pFilename,
                                                mz_uint filename_buf_size);




MINIZ_EXPORT int mz_zip_reader_locate_file(mz_zip_archive* pZip, const char* pName, const char* pComment,
                                           mz_uint flags);
MINIZ_EXPORT mz_bool mz_zip_reader_locate_file_v2(mz_zip_archive* pZip, const char* pName, const char* pComment,
                                                  mz_uint flags, mz_uint32* file_index);


MINIZ_EXPORT mz_bool mz_zip_reader_file_stat(mz_zip_archive* pZip, mz_uint file_index, mz_zip_archive_file_stat* pStat);



MINIZ_EXPORT mz_bool mz_zip_is_zip64(mz_zip_archive* pZip);



MINIZ_EXPORT size_t mz_zip_get_central_dir_size(mz_zip_archive* pZip);



MINIZ_EXPORT mz_bool mz_zip_reader_extract_to_mem_no_alloc(mz_zip_archive* pZip, mz_uint file_index, void* pBuf,
                                                           size_t buf_size, mz_uint flags, void* pUser_read_buf,
                                                           size_t user_read_buf_size);
MINIZ_EXPORT mz_bool mz_zip_reader_extract_file_to_mem_no_alloc(mz_zip_archive* pZip, const char* pFilename, void* pBuf,
                                                                size_t buf_size, mz_uint flags, void* pUser_read_buf,
                                                                size_t user_read_buf_size);


MINIZ_EXPORT mz_bool mz_zip_reader_extract_to_mem(mz_zip_archive* pZip, mz_uint file_index, void* pBuf, size_t buf_size,
                                                  mz_uint flags);
MINIZ_EXPORT mz_bool mz_zip_reader_extract_file_to_mem(mz_zip_archive* pZip, const char* pFilename, void* pBuf,
                                                       size_t buf_size, mz_uint flags);




MINIZ_EXPORT void* mz_zip_reader_extract_to_heap(mz_zip_archive* pZip, mz_uint file_index, size_t* pSize,
                                                 mz_uint flags);
MINIZ_EXPORT void* mz_zip_reader_extract_file_to_heap(mz_zip_archive* pZip, const char* pFilename, size_t* pSize,
                                                      mz_uint flags);


MINIZ_EXPORT mz_bool mz_zip_reader_extract_to_callback(mz_zip_archive* pZip, mz_uint file_index,
                                                       mz_file_write_func pCallback, void* pOpaque, mz_uint flags);
MINIZ_EXPORT mz_bool mz_zip_reader_extract_file_to_callback(mz_zip_archive* pZip, const char* pFilename,
                                                            mz_file_write_func pCallback, void* pOpaque, mz_uint flags);


MINIZ_EXPORT mz_zip_reader_extract_iter_state* mz_zip_reader_extract_iter_new(mz_zip_archive* pZip, mz_uint file_index,
                                                                              mz_uint flags);
MINIZ_EXPORT mz_zip_reader_extract_iter_state* mz_zip_reader_extract_file_iter_new(mz_zip_archive* pZip,
                                                                                   const char* pFilename,
                                                                                   mz_uint flags);
MINIZ_EXPORT size_t mz_zip_reader_extract_iter_read(mz_zip_reader_extract_iter_state* pState, void* pvBuf,
                                                    size_t buf_size);
MINIZ_EXPORT mz_bool mz_zip_reader_extract_iter_free(mz_zip_reader_extract_iter_state* pState);

#ifndef MINIZ_NO_STDIO


MINIZ_EXPORT mz_bool mz_zip_reader_extract_to_file(mz_zip_archive* pZip, mz_uint file_index, const char* pDst_filename,
                                                   mz_uint flags);
MINIZ_EXPORT mz_bool mz_zip_reader_extract_file_to_file(mz_zip_archive* pZip, const char* pArchive_filename,
                                                        const char* pDst_filename, mz_uint flags);


MINIZ_EXPORT mz_bool mz_zip_reader_extract_to_cfile(mz_zip_archive* pZip, mz_uint file_index, MZ_FILE* File,
                                                    mz_uint flags);
MINIZ_EXPORT mz_bool mz_zip_reader_extract_file_to_cfile(mz_zip_archive* pZip, const char* pArchive_filename,
                                                         MZ_FILE* pFile, mz_uint flags);
#endif

#if 0

	typedef void *mz_zip_streaming_extract_state_ptr;
	mz_zip_streaming_extract_state_ptr mz_zip_streaming_extract_begin(mz_zip_archive *pZip, mz_uint file_index, mz_uint flags);
	uint64_t mz_zip_streaming_extract_get_size(mz_zip_archive *pZip, mz_zip_streaming_extract_state_ptr pState);
	uint64_t mz_zip_streaming_extract_get_cur_ofs(mz_zip_archive *pZip, mz_zip_streaming_extract_state_ptr pState);
	mz_bool mz_zip_streaming_extract_seek(mz_zip_archive *pZip, mz_zip_streaming_extract_state_ptr pState, uint64_t new_ofs);
	size_t mz_zip_streaming_extract_read(mz_zip_archive *pZip, mz_zip_streaming_extract_state_ptr pState, void *pBuf, size_t buf_size);
	mz_bool mz_zip_streaming_extract_end(mz_zip_archive *pZip, mz_zip_streaming_extract_state_ptr pState);
#endif



MINIZ_EXPORT mz_bool mz_zip_validate_file(mz_zip_archive* pZip, mz_uint file_index, mz_uint flags);


MINIZ_EXPORT mz_bool mz_zip_validate_archive(mz_zip_archive* pZip, mz_uint flags);


MINIZ_EXPORT mz_bool mz_zip_validate_mem_archive(const void* pMem, size_t size, mz_uint flags, mz_zip_error* pErr);
MINIZ_EXPORT mz_bool mz_zip_validate_file_archive(const char* pFilename, mz_uint flags, mz_zip_error* pErr);


MINIZ_EXPORT mz_bool mz_zip_end(mz_zip_archive* pZip);



#ifndef MINIZ_NO_ARCHIVE_WRITING_APIS




MINIZ_EXPORT mz_bool mz_zip_writer_init(mz_zip_archive* pZip, mz_uint64 existing_size);
MINIZ_EXPORT mz_bool mz_zip_writer_init_v2(mz_zip_archive* pZip, mz_uint64 existing_size, mz_uint flags);

MINIZ_EXPORT mz_bool mz_zip_writer_init_heap(mz_zip_archive* pZip, size_t size_to_reserve_at_beginning,
                                             size_t initial_allocation_size);
MINIZ_EXPORT mz_bool mz_zip_writer_init_heap_v2(mz_zip_archive* pZip, size_t size_to_reserve_at_beginning,
                                                size_t initial_allocation_size, mz_uint flags);

#ifndef MINIZ_NO_STDIO
MINIZ_EXPORT mz_bool mz_zip_writer_init_file(mz_zip_archive* pZip, const char* pFilename,
                                             mz_uint64 size_to_reserve_at_beginning);
MINIZ_EXPORT mz_bool mz_zip_writer_init_file_v2(mz_zip_archive* pZip, const char* pFilename,
                                                mz_uint64 size_to_reserve_at_beginning, mz_uint flags);
MINIZ_EXPORT mz_bool mz_zip_writer_init_cfile(mz_zip_archive* pZip, MZ_FILE* pFile, mz_uint flags);
#endif







MINIZ_EXPORT mz_bool mz_zip_writer_init_from_reader(mz_zip_archive* pZip, const char* pFilename);
MINIZ_EXPORT mz_bool mz_zip_writer_init_from_reader_v2(mz_zip_archive* pZip, const char* pFilename, mz_uint flags);




MINIZ_EXPORT mz_bool mz_zip_writer_add_mem(mz_zip_archive* pZip, const char* pArchive_name, const void* pBuf,
                                           size_t buf_size, mz_uint level_and_flags);



MINIZ_EXPORT mz_bool mz_zip_writer_add_mem_ex(mz_zip_archive* pZip, const char* pArchive_name, const void* pBuf,
                                              size_t buf_size, const void* pComment, mz_uint16 comment_size,
                                              mz_uint level_and_flags, mz_uint64 uncomp_size, mz_uint32 uncomp_crc32);

MINIZ_EXPORT mz_bool mz_zip_writer_add_mem_ex_v2(mz_zip_archive* pZip, const char* pArchive_name, const void* pBuf,
                                                 size_t buf_size, const void* pComment, mz_uint16 comment_size,
                                                 mz_uint level_and_flags, mz_uint64 uncomp_size, mz_uint32 uncomp_crc32,
                                                 MZ_TIME_T* last_modified, const char* user_extra_data_local,
                                                 mz_uint user_extra_data_local_len, const char* user_extra_data_central,
                                                 mz_uint user_extra_data_central_len);



MINIZ_EXPORT mz_bool mz_zip_writer_add_read_buf_callback(
    mz_zip_archive* pZip, const char* pArchive_name, mz_file_read_func read_callback, void* callback_opaque,
    mz_uint64 max_size, const MZ_TIME_T* pFile_time, const void* pComment, mz_uint16 comment_size,
    mz_uint level_and_flags, const char* user_extra_data_local, mz_uint user_extra_data_local_len,
    const char* user_extra_data_central, mz_uint user_extra_data_central_len);

#ifndef MINIZ_NO_STDIO


MINIZ_EXPORT mz_bool mz_zip_writer_add_file(mz_zip_archive* pZip, const char* pArchive_name, const char* pSrc_filename,
                                            const void* pComment, mz_uint16 comment_size, mz_uint level_and_flags);


MINIZ_EXPORT mz_bool mz_zip_writer_add_cfile(mz_zip_archive* pZip, const char* pArchive_name, MZ_FILE* pSrc_file,
                                             mz_uint64 max_size, const MZ_TIME_T* pFile_time, const void* pComment,
                                             mz_uint16 comment_size, mz_uint level_and_flags,
                                             const char* user_extra_data_local, mz_uint user_extra_data_local_len,
                                             const char* user_extra_data_central, mz_uint user_extra_data_central_len);
#endif



MINIZ_EXPORT mz_bool mz_zip_writer_add_from_zip_reader(mz_zip_archive* pZip, mz_zip_archive* pSource_zip,
                                                       mz_uint src_file_index);




MINIZ_EXPORT mz_bool mz_zip_writer_finalize_archive(mz_zip_archive* pZip);



MINIZ_EXPORT mz_bool mz_zip_writer_finalize_heap_archive(mz_zip_archive* pZip, void** ppBuf, size_t* pSize);



MINIZ_EXPORT mz_bool mz_zip_writer_end(mz_zip_archive* pZip);







MINIZ_EXPORT mz_bool mz_zip_add_mem_to_archive_file_in_place(const char* pZip_filename, const char* pArchive_name,
                                                             const void* pBuf, size_t buf_size, const void* pComment,
                                                             mz_uint16 comment_size, mz_uint level_and_flags);
MINIZ_EXPORT mz_bool mz_zip_add_mem_to_archive_file_in_place_v2(const char* pZip_filename, const char* pArchive_name,
                                                                const void* pBuf, size_t buf_size, const void* pComment,
                                                                mz_uint16 comment_size, mz_uint level_and_flags,
                                                                mz_zip_error* pErr);




MINIZ_EXPORT void* mz_zip_extract_archive_file_to_heap(const char* pZip_filename, const char* pArchive_name,
                                                       size_t* pSize, mz_uint flags);
MINIZ_EXPORT void* mz_zip_extract_archive_file_to_heap_v2(const char* pZip_filename, const char* pArchive_name,
                                                          const char* pComment, size_t* pSize, mz_uint flags,
                                                          mz_zip_error* pErr);

#endif 

#ifdef __cplusplus
}
#endif

#endif 
