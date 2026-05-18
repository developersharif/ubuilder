/*
 * SHA-256 — straightforward FIPS 180-4 implementation, no external deps.
 * Public-domain / CC0. Audit-friendly: every byte fed in is hashed exactly
 * once; the only state is in `ub_sha256_ctx`.
 */

#include "sha256.h"
#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BSIG0(x)   (ROTR(x, 2)  ^ ROTR(x, 13) ^ ROTR(x, 22))
#define BSIG1(x)   (ROTR(x, 6)  ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SSIG0(x)   (ROTR(x, 7)  ^ ROTR(x, 18) ^ ((x) >> 3))
#define SSIG1(x)   (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static void sha256_compress(uint32_t s[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] =  ((uint32_t)block[i*4]     << 24) |
                ((uint32_t)block[i*4 + 1] << 16) |
                ((uint32_t)block[i*4 + 2] <<  8) |
                ((uint32_t)block[i*4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        w[i] = SSIG1(w[i-2]) + w[i-7] + SSIG0(w[i-15]) + w[i-16];
    }
    uint32_t a = s[0], b = s[1], c = s[2], d = s[3];
    uint32_t e = s[4], f = s[5], g = s[6], h = s[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + BSIG1(e) + CH(e,f,g) + K[i] + w[i];
        uint32_t t2 = BSIG0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    s[0] += a; s[1] += b; s[2] += c; s[3] += d;
    s[4] += e; s[5] += f; s[6] += g; s[7] += h;
}

void ub_sha256_init(ub_sha256_ctx* c) {
    c->state[0] = 0x6a09e667; c->state[1] = 0xbb67ae85;
    c->state[2] = 0x3c6ef372; c->state[3] = 0xa54ff53a;
    c->state[4] = 0x510e527f; c->state[5] = 0x9b05688c;
    c->state[6] = 0x1f83d9ab; c->state[7] = 0x5be0cd19;
    c->bit_len  = 0;
    c->buf_len  = 0;
}

void ub_sha256_update(ub_sha256_ctx* c, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    c->bit_len += (uint64_t)len * 8;

    /* Fill any partial block first. */
    if (c->buf_len) {
        size_t need = 64 - c->buf_len;
        if (len < need) {
            memcpy(c->buf + c->buf_len, p, len);
            c->buf_len += len;
            return;
        }
        memcpy(c->buf + c->buf_len, p, need);
        sha256_compress(c->state, c->buf);
        p   += need;
        len -= need;
        c->buf_len = 0;
    }
    /* Process full 64-byte blocks straight from input. */
    while (len >= 64) {
        sha256_compress(c->state, p);
        p   += 64;
        len -= 64;
    }
    /* Save the tail. */
    if (len) {
        memcpy(c->buf, p, len);
        c->buf_len = len;
    }
}

void ub_sha256_final(ub_sha256_ctx* c, uint8_t out[UB_SHA256_DIGEST_SIZE]) {
    /* Append 0x80, then zero-pad to (mod 64) == 56, then 8-byte length. */
    c->buf[c->buf_len++] = 0x80;
    if (c->buf_len > 56) {
        memset(c->buf + c->buf_len, 0, 64 - c->buf_len);
        sha256_compress(c->state, c->buf);
        c->buf_len = 0;
    }
    memset(c->buf + c->buf_len, 0, 56 - c->buf_len);
    uint64_t bl = c->bit_len;
    for (int i = 0; i < 8; i++) c->buf[63 - i] = (uint8_t)(bl >> (i * 8));
    sha256_compress(c->state, c->buf);

    for (int i = 0; i < 8; i++) {
        out[i*4]     = (uint8_t)(c->state[i] >> 24);
        out[i*4 + 1] = (uint8_t)(c->state[i] >> 16);
        out[i*4 + 2] = (uint8_t)(c->state[i] >>  8);
        out[i*4 + 3] = (uint8_t)(c->state[i]);
    }
}

void ub_sha256(const void* data, size_t len, uint8_t out[UB_SHA256_DIGEST_SIZE]) {
    ub_sha256_ctx c;
    ub_sha256_init(&c);
    ub_sha256_update(&c, data, len);
    ub_sha256_final(&c, out);
}

int ub_sha256_file_range(FILE* f, long start_offset, long length,
                         uint8_t out[UB_SHA256_DIGEST_SIZE]) {
    if (!f || length < 0) return -1;
    long saved = ftell(f);
    if (saved < 0) return -1;
    if (fseek(f, start_offset, SEEK_SET) != 0) return -1;

    ub_sha256_ctx c;
    ub_sha256_init(&c);
    uint8_t buf[65536];
    long remaining = length;
    int  rc = 0;
    while (remaining > 0) {
        size_t want = (remaining > (long)sizeof(buf)) ? sizeof(buf) : (size_t)remaining;
        size_t got  = fread(buf, 1, want, f);
        if (got == 0) { rc = -1; break; }
        ub_sha256_update(&c, buf, got);
        remaining -= (long)got;
    }
    if (rc == 0) ub_sha256_final(&c, out);

    /* Restore caller's position even on failure. */
    if (fseek(f, saved, SEEK_SET) != 0) rc = -1;
    return rc;
}

void ub_sha256_hex(const uint8_t in[UB_SHA256_DIGEST_SIZE], char out[65]) {
    static const char* H = "0123456789abcdef";
    for (int i = 0; i < UB_SHA256_DIGEST_SIZE; i++) {
        out[i*2]     = H[(in[i] >> 4) & 0xF];
        out[i*2 + 1] = H[in[i]        & 0xF];
    }
    out[64] = 0;
}
