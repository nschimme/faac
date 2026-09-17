/*
 * Shared Portable Endianness and High-Performance Byte Swapping Utilities for FAAC
 */

#ifndef FAAC_ENDIAN_H
#define FAAC_ENDIAN_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Endianness detection */
#if (defined(WORDS_BIGENDIAN) && WORDS_BIGENDIAN) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) || \
    (defined(__BIG_ENDIAN__) && !defined(__LITTLE_ENDIAN__))
#  define FAAC_IS_BIG_ENDIAN 1
#else
#  define FAAC_IS_BIG_ENDIAN 0
#endif

/* High-performance byte swapping primitives using compiler builtins */
#if defined(__has_builtin)
#  if __has_builtin(__builtin_bswap16) && __has_builtin(__builtin_bswap32) && __has_builtin(__builtin_bswap64)
#    define FAAC_BSWAP16(x) __builtin_bswap16((uint16_t)(x))
#    define FAAC_BSWAP32(x) __builtin_bswap32((uint32_t)(x))
#    define FAAC_BSWAP64(x) __builtin_bswap64((uint64_t)(x))
#  endif
#elif defined(__GNUC__)
#  define FAAC_BSWAP16(x) __builtin_bswap16((uint16_t)(x))
#  define FAAC_BSWAP32(x) __builtin_bswap32((uint32_t)(x))
#  define FAAC_BSWAP64(x) __builtin_bswap64((uint64_t)(x))
#elif defined(_MSC_VER)
#  include <stdlib.h>
#  define FAAC_BSWAP16(x) _byteswap_ushort((uint16_t)(x))
#  define FAAC_BSWAP32(x) _byteswap_ulong((uint32_t)(x))
#  define FAAC_BSWAP64(x) _byteswap_uint64((uint64_t)(x))
#endif

#ifndef FAAC_BSWAP16
static inline uint16_t FAAC_BSWAP16(uint16_t x) {
    return (uint16_t)((x >> 8) | (x << 8));
}
#endif

#ifndef FAAC_BSWAP32
static inline uint32_t FAAC_BSWAP32(uint32_t x) {
    return (x >> 24) | ((x >> 8) & 0x0000FF00U) | ((x << 8) & 0x00FF0000U) | (x << 24);
}
#endif

#ifndef FAAC_BSWAP64
static inline uint64_t FAAC_BSWAP64(uint64_t x) {
    return ((x >> 56) & 0x00000000000000FFULL) |
           ((x >> 40) & 0x000000000000FF00ULL) |
           ((x >> 24) & 0x0000000000FF0000ULL) |
           ((x >> 8)  & 0x00000000FF000000ULL) |
           ((x << 8)  & 0x000000FF00000000ULL) |
           ((x << 24) & 0x0000FF0000000000ULL) |
           ((x << 40) & 0x00FF000000000000ULL) |
           ((x << 56) & 0xFF00000000000000ULL);
}
#endif

/* Big-Endian Memory Reads */
static inline uint16_t read_u16_be(const uint8_t *b) {
#if FAAC_IS_BIG_ENDIAN
    uint16_t v;
    memcpy(&v, b, sizeof(v));
    return v;
#else
    uint16_t v;
    memcpy(&v, b, sizeof(v));
    return FAAC_BSWAP16(v);
#endif
}

static inline uint32_t read_u32_be(const uint8_t *b) {
#if FAAC_IS_BIG_ENDIAN
    uint32_t v;
    memcpy(&v, b, sizeof(v));
    return v;
#else
    uint32_t v;
    memcpy(&v, b, sizeof(v));
    return FAAC_BSWAP32(v);
#endif
}

static inline uint64_t read_u64_be(const uint8_t *b) {
#if FAAC_IS_BIG_ENDIAN
    uint64_t v;
    memcpy(&v, b, sizeof(v));
    return v;
#else
    uint64_t v;
    memcpy(&v, b, sizeof(v));
    return FAAC_BSWAP64(v);
#endif
}

/* Big-Endian Memory Writes */
static inline void write_u16_be(uint8_t *b, uint16_t val) {
#if FAAC_IS_BIG_ENDIAN
    memcpy(b, &val, sizeof(val));
#else
    uint16_t v = FAAC_BSWAP16(val);
    memcpy(b, &v, sizeof(v));
#endif
}

static inline void write_u32_be(uint8_t *b, uint32_t val) {
#if FAAC_IS_BIG_ENDIAN
    memcpy(b, &val, sizeof(val));
#else
    uint32_t v = FAAC_BSWAP32(val);
    memcpy(b, &v, sizeof(v));
#endif
}

static inline void write_u64_be(uint8_t *b, uint64_t val) {
#if FAAC_IS_BIG_ENDIAN
    memcpy(b, &val, sizeof(val));
#else
    uint64_t v = FAAC_BSWAP64(val);
    memcpy(b, &v, sizeof(v));
#endif
}

/* Little-Endian Memory Reads */
static inline uint16_t read_u16_le(const uint8_t *b) {
#if FAAC_IS_BIG_ENDIAN
    uint16_t v;
    memcpy(&v, b, sizeof(v));
    return FAAC_BSWAP16(v);
#else
    uint16_t v;
    memcpy(&v, b, sizeof(v));
    return v;
#endif
}

static inline uint32_t read_u32_le(const uint8_t *b) {
#if FAAC_IS_BIG_ENDIAN
    uint32_t v;
    memcpy(&v, b, sizeof(v));
    return FAAC_BSWAP32(v);
#else
    uint32_t v;
    memcpy(&v, b, sizeof(v));
    return v;
#endif
}

/* Little-Endian Memory Writes */
static inline void write_u16_le(uint8_t *b, uint16_t val) {
#if FAAC_IS_BIG_ENDIAN
    uint16_t v = FAAC_BSWAP16(val);
    memcpy(b, &v, sizeof(v));
#else
    memcpy(b, &val, sizeof(val));
#endif
}

static inline void write_u32_le(uint8_t *b, uint32_t val) {
#if FAAC_IS_BIG_ENDIAN
    uint32_t v = FAAC_BSWAP32(val);
    memcpy(b, &v, sizeof(v));
#else
    memcpy(b, &val, sizeof(val));
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* FAAC_ENDIAN_H */
