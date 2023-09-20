/* This code is public-domain - it is based on libcrypt
 * placed in the public domain by Wei Dai and other contributors.
 */
// gcc -Wall -DSHA1TEST -o sha1test sha1.c && ./sha1test

#ifndef SHA1_H_INCLUDED
#define SHA1_H_INCLUDED

#include <stdint.h>
#include <string.h>

#ifdef __BIG_ENDIAN__
# define SHA_BIG_ENDIAN
#elif defined __LITTLE_ENDIAN__
/* override */
#elif defined __BYTE_ORDER
# if __BYTE_ORDER__ ==  __ORDER_BIG_ENDIAN__
# define SHA_BIG_ENDIAN
# endif
#elif defined _WIN32
/* assume little endian */
#else
# include <endian.h> // machine/endian.h
# if __BYTE_ORDER__ ==  __ORDER_BIG_ENDIAN__
#  define SHA_BIG_ENDIAN
# endif
#endif

/* header */

#define HASH_LENGTH 20
#define BLOCK_LENGTH 64

typedef struct sha1nfo {
    uint32_t buffer[BLOCK_LENGTH/4];
    uint32_t state[HASH_LENGTH/4];
    uint32_t byteCount;
    uint8_t bufferOffset;
    uint8_t keyBuffer[BLOCK_LENGTH];
    uint8_t innerHash[HASH_LENGTH];
} sha1nfo;

/* public API - prototypes */

/**
 */
void sha1_init(sha1nfo *s);
/**
 */
void sha1_writebyte(sha1nfo *s, uint8_t data);
/**
 */
void sha1_write(sha1nfo *s, const char *data, size_t len);
/**
 */
uint8_t* sha1_result(sha1nfo *s);

#endif // SHA1_H_INCLUDED
