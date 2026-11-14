/* SHA-2 hashes. */
#include "bs.h"
#include "crypto.h"

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

/* ---- SHA-256 ----------------------------------------------------------- */

static const u32 sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static u32 ror(u32 v, u32 n)
{
    return v >> n | v << (32 - n);
}

void sha_init(struct sha256 *s)
{
    static const u32 start[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    memcpy(s->h, start, sizeof(start));
    s->used = 0;
    s->total = 0;
}

static void sha_block(struct sha256 *s, const u8 *p)
{
    u32 w[64], v[8];

    for (u32 i = 0; i < 16; i++)
        w[i] = (u32)p[i * 4] << 24 | (u32)p[i * 4 + 1] << 16 | (u32)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (u32 i = 16; i < 64; i++) {
        u32 a = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ w[i - 15] >> 3;
        u32 b = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ w[i - 2] >> 10;

        w[i] = w[i - 16] + a + w[i - 7] + b;
    }
    memcpy(v, s->h, sizeof(v));
    for (u32 i = 0; i < 64; i++) {
        u32 t1 = v[7] + (ror(v[4], 6) ^ ror(v[4], 11) ^ ror(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) +
                 sha_k[i] + w[i];
        u32 t2 = (ror(v[0], 2) ^ ror(v[0], 13) ^ ror(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));

        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + t2;
    }
    for (u32 i = 0; i < 8; i++)
        s->h[i] += v[i];
}

void sha_update(struct sha256 *s, const void *data, u32 length)
{
    const u8 *p = data;

    s->total += length;
    while (length) {
        u32 chunk = 64 - s->used < length ? 64 - s->used : length;

        memcpy(s->block + s->used, p, chunk);
        s->used += chunk;
        p += chunk;
        length -= chunk;
        if (s->used == 64) {
            sha_block(s, s->block);
            s->used = 0;
        }
    }
}

void sha_final(struct sha256 *s, u8 *out)
{
    u64 bits = s->total * 8;
    u8 length[8];

    for (int i = 0; i < 8; i++)
        length[i] = (u8)(bits >> (56 - i * 8));
    sha_update(s, "\x80", 1);
    while (s->used != 56)
        sha_update(s, "\0", 1);
    sha_update(s, length, 8);
    for (u32 i = 0; i < 8; i++) {
        out[i * 4] = (u8)(s->h[i] >> 24);
        out[i * 4 + 1] = (u8)(s->h[i] >> 16);
        out[i * 4 + 2] = (u8)(s->h[i] >> 8);
        out[i * 4 + 3] = (u8)s->h[i];
    }
}


/* ---- SHA-384 and SHA-512 ------------------------------------------------- */

static const u64 k512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

static const u64 init512[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

static const u64 init384[8] = {
    0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
    0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL,
};

static u64 ror64(u64 v, u32 n)
{
    return v >> n | v << (64 - n);
}

static void sha512_block(u64 *h, const u8 *p)
{
    u64 w[80], v[8];

    for (u32 i = 0; i < 16; i++) {
        u64 value = 0;

        for (u32 j = 0; j < 8; j++)
            value = value << 8 | p[i * 8 + j];
        w[i] = value;
    }
    for (u32 i = 16; i < 80; i++) {
        u64 a = ror64(w[i - 15], 1) ^ ror64(w[i - 15], 8) ^ w[i - 15] >> 7;
        u64 b = ror64(w[i - 2], 19) ^ ror64(w[i - 2], 61) ^ w[i - 2] >> 6;

        w[i] = w[i - 16] + a + w[i - 7] + b;
    }
    memcpy(v, h, sizeof(v));
    for (u32 i = 0; i < 80; i++) {
        u64 t1 = v[7] + (ror64(v[4], 14) ^ ror64(v[4], 18) ^ ror64(v[4], 41)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) +
                 k512[i] + w[i];
        u64 t2 = (ror64(v[0], 28) ^ ror64(v[0], 34) ^ ror64(v[0], 39)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));

        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + t2;
    }
    for (u32 i = 0; i < 8; i++)
        h[i] += v[i];
}

static void sha512_digest(u32 size, const u8 *data, u32 length, u8 *out)
{
    u64 h[8];
    u8 last[256];
    u32 remaining = length, used;
    u64 bits = (u64)length * 8;

    memcpy(h, size == HASH_SHA384 ? init384 : init512, sizeof(h));
    while (remaining >= 128) {
        sha512_block(h, data);
        data += 128;
        remaining -= 128;
    }
    memset(last, 0, sizeof(last));
    memcpy(last, data, remaining);
    last[remaining] = 0x80;
    used = remaining + 17 <= 128 ? 128 : 256;
    for (u32 i = 0; i < 8; i++)
        last[used - 1 - i] = (u8)(bits >> (i * 8));
    sha512_block(h, last);
    if (used == 256)
        sha512_block(h, last + 128);

    for (u32 i = 0; i < size; i++)
        out[i] = (u8)(h[i / 8] >> (56 - i % 8 * 8));
}

void hash_digest(uint32_t algorithm, const void *data, uint32_t length, uint8_t *out)
{
    if (algorithm == HASH_SHA256) {
        struct sha256 s;

        sha_init(&s);
        sha_update(&s, data, length);
        sha_final(&s, out);
    } else {
        sha512_digest(algorithm, data, length, out);
    }
}
