#ifndef ME_DEBUG_H
#define ME_DEBUG_H

#include <nfp.h>
#include <nfp/mem_atomic.h>
#include <nfp/mem_bulk.h>

#define CEIL(X, Y) (((X)/(Y)) + (((X) % (Y) == 0) ? 0 : 1))

extern __volatile __shared __emem uint32_t debug[4096 * 64];
extern __volatile __shared __emem uint32_t debug_idx;

/* DEBUG MACROS */
#define DEBUG(_a, _b, _c, _d) \
do {\
    __xrw uint32_t _idx_val = 4; \
    __xwrite uint32_t _dvals[4]; \
    mem_test_add(&_idx_val, \
            (__mem40 void *)&debug_idx, sizeof(_idx_val)); \
\
    _dvals[0] = _a; \
    _dvals[1] = _b; \
    _dvals[2] = _c; \
    _dvals[3] = _d; \
\
    mem_write_atomic(_dvals, (__mem40 void *)\
                    (debug + (_idx_val % (1024 * 64))), sizeof(_dvals)); \
} while(0)

#define DEBUG_MEM(_a, _len) \
do {\
    int i; \
    __xrw uint32_t _idx_val = 4 + CEIL(_len, 4); \
    __xwrite uint32_t _dvals[4]; \
    mem_test_add(&_idx_val, \
            (__mem40 void *)&debug_idx, \
            sizeof(_idx_val)); \
\
    _dvals[0] = 0x87654321; \
    _dvals[1] = (uint32_t) (((uint64_t)_a) >> 32); \
    _dvals[2] = (uint32_t) (((uint64_t)_a) & 0xFFFFFFFF); \
    _dvals[3] = _len; \
\
    mem_write_atomic(_dvals, (__mem40 void *)\
                    (debug + (_idx_val % (1024 * 64))), sizeof(_dvals)); \
\
    for (i = 0; i < (_len+15)/16; i++) { \
      _dvals[0] = i*16 < _len ? *((__mem40 uint32_t *)_a+i*4) : 0x12345678; \
      _dvals[1] = i*16 + 4 < _len ? *((__mem40 uint32_t *)_a+i*4+1) : 0x12345678; \
      _dvals[2] = i*16 + 8 < _len ? *((__mem40 uint32_t *)_a+i*4+2) : 0x12345678; \
      _dvals[3] = i*16 + 12 < _len ? *((__mem40 uint32_t *)_a+i*4+3) : 0x12345678; \
      mem_write_atomic(_dvals, (__mem40 void *)\
                      (debug + (_idx_val % (1024 * 64))), sizeof(_dvals)); \
    } \
} while(0)

#endif /* ME_DEBUG_H */
