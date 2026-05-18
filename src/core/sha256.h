#ifndef UBUILDER_SHA256_H
#define UBUILDER_SHA256_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UB_SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t  buf[64];
    size_t   buf_len;
} ub_sha256_ctx;

void ub_sha256_init(ub_sha256_ctx* c);
void ub_sha256_update(ub_sha256_ctx* c, const void* data, size_t len);
void ub_sha256_final(ub_sha256_ctx* c, uint8_t out[UB_SHA256_DIGEST_SIZE]);

/* One-shot. */
void ub_sha256(const void* data, size_t len, uint8_t out[UB_SHA256_DIGEST_SIZE]);

/*
 * Hash a slice of a file: bytes [start_offset, start_offset + length).
 * Reads in 64 KiB chunks; the file position is preserved. Returns 0 on
 * success, -1 on I/O error. The file must be opened in binary mode.
 */
int ub_sha256_file_range(FILE* f, long start_offset, long length,
                         uint8_t out[UB_SHA256_DIGEST_SIZE]);

/* Hex-encode a digest into a 65-byte buffer (64 hex chars + NUL). */
void ub_sha256_hex(const uint8_t in[UB_SHA256_DIGEST_SIZE], char out[65]);

#ifdef __cplusplus
}
#endif

#endif
