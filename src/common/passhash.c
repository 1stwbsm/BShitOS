/* PBKDF2-HMAC-SHA256 password records. Freestanding: no library calls. */
#include <bshit/passhash.h>

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t ror(uint32_t v, uint32_t n)
{
    return v >> n | v << (32 - n);
}

static void compress(struct pass_sha256 *s, const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, h;

    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));

        h = g, g = f, f = e, e = d + t1;
        d = c, c = b, b = a, a = t1 + t2;
    }
    s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d;
    s->h[4] += e, s->h[5] += f, s->h[6] += g, s->h[7] += h;
}

void pass_sha256_init(struct pass_sha256 *s)
{
    static const uint32_t start[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    for (int i = 0; i < 8; i++)
        s->h[i] = start[i];
    s->used = 0;
    s->total = 0;
}

void pass_sha256_update(struct pass_sha256 *s, const void *data, uint32_t length)
{
    const uint8_t *p = data;

    s->total += length;
    while (length--) {
        s->block[s->used++] = *p++;
        if (s->used == 64) {
            compress(s, s->block);
            s->used = 0;
        }
    }
}

void pass_sha256_final(struct pass_sha256 *s, uint8_t out[32])
{
    uint64_t bits = s->total * 8;
    uint8_t pad = 0x80, zero = 0, length[8];

    pass_sha256_update(s, &pad, 1);
    while (s->used != 56)
        pass_sha256_update(s, &zero, 1);
    for (int i = 0; i < 8; i++)
        length[i] = (uint8_t)(bits >> (56 - 8 * i));
    pass_sha256_update(s, length, 8);
    for (int i = 0; i < 8; i++) {
        out[i * 4] = (uint8_t)(s->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)s->h[i];
    }
}

static uint32_t length_of(const char *s)
{
    uint32_t n = 0;

    while (s[n])
        n++;
    return n;
}

/* HMAC-SHA256 with the key already folded into the inner and outer pads. */
struct hmac {
    struct pass_sha256 inner, outer;
};

static void hmac_key(struct hmac *m, const char *key)
{
    uint8_t pad[64], folded[32];
    uint32_t key_length = length_of(key);
    const uint8_t *k = (const uint8_t *)key;

    if (key_length > 64) {
        struct pass_sha256 s;

        pass_sha256_init(&s);
        pass_sha256_update(&s, key, key_length);
        pass_sha256_final(&s, folded);
        k = folded;
        key_length = 32;
    }
    for (uint32_t i = 0; i < 64; i++)
        pad[i] = (uint8_t)((i < key_length ? k[i] : 0) ^ 0x36);
    pass_sha256_init(&m->inner);
    pass_sha256_update(&m->inner, pad, 64);
    for (uint32_t i = 0; i < 64; i++)
        pad[i] ^= 0x36 ^ 0x5c;
    pass_sha256_init(&m->outer);
    pass_sha256_update(&m->outer, pad, 64);
}

static void hmac(const struct hmac *keyed, const uint8_t *data, uint32_t length, uint8_t out[32])
{
    struct hmac m = *keyed;
    uint8_t inner[32];

    pass_sha256_update(&m.inner, data, length);
    pass_sha256_final(&m.inner, inner);
    pass_sha256_update(&m.outer, inner, 32);
    pass_sha256_final(&m.outer, out);
}

/* One block of PBKDF2 output, which is all a 32-byte hash needs. */
static void pbkdf2(const char *password, const uint8_t *salt, uint32_t iterations, uint8_t out[32])
{
    struct hmac keyed;
    uint8_t first[PASS_SALT_BYTES + 4], u[32];

    hmac_key(&keyed, password);
    for (uint32_t i = 0; i < PASS_SALT_BYTES; i++)
        first[i] = salt[i];
    first[PASS_SALT_BYTES] = 0, first[PASS_SALT_BYTES + 1] = 0;
    first[PASS_SALT_BYTES + 2] = 0, first[PASS_SALT_BYTES + 3] = 1;
    hmac(&keyed, first, sizeof(first), u);
    for (int i = 0; i < 32; i++)
        out[i] = u[i];
    for (uint32_t n = 1; n < iterations; n++) {
        hmac(&keyed, u, 32, u);
        for (int i = 0; i < 32; i++)
            out[i] ^= u[i];
    }
}

static const char hex_digits[] = "0123456789abcdef";

static char *put_hex(char *p, const uint8_t *bytes, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        *p++ = hex_digits[bytes[i] >> 4];
        *p++ = hex_digits[bytes[i] & 15];
    }
    return p;
}

static int get_hex(const char *p, uint8_t *bytes, uint32_t count)
{
    for (uint32_t i = 0; i < count * 2; i++) {
        char c = p[i];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;

        if (v < 0)
            return -1;
        bytes[i / 2] = (uint8_t)(i % 2 ? bytes[i / 2] | v : v << 4);
    }
    return 0;
}

int pass_make_record(const char *password, const uint8_t salt[PASS_SALT_BYTES], char *out, uint32_t size)
{
    uint8_t hash[PASS_HASH_BYTES];
    char digits[12], *p = out;
    uint32_t n = 0, iterations = PASS_ITERATIONS;

    if (size < PASS_RECORD_MAX)
        return -1;
    if (password[0] == 0) {
        out[0] = 0; /* empty password: an empty record */
        return 0;
    }
    pbkdf2(password, salt, iterations, hash);
    do {
        digits[n++] = (char)('0' + iterations % 10);
        iterations /= 10;
    } while (iterations);
    while (n)
        *p++ = digits[--n];
    *p++ = ':';
    p = put_hex(p, salt, PASS_SALT_BYTES);
    *p++ = ':';
    p = put_hex(p, hash, PASS_HASH_BYTES);
    *p = 0;
    return 0;
}

int pass_check(const char *password, const char *record)
{
    uint8_t salt[PASS_SALT_BYTES], want[PASS_HASH_BYTES], got[PASS_HASH_BYTES], diff = 0;
    uint32_t iterations = 0;
    const char *p = record;

    if (record[0] == 0)
        return password[0] == 0;
    while (*p >= '0' && *p <= '9' && iterations < 10000000u)
        iterations = iterations * 10 + (uint32_t)(*p++ - '0');
    if (iterations == 0 || *p++ != ':' || get_hex(p, salt, PASS_SALT_BYTES) < 0)
        return 0;
    p += PASS_SALT_BYTES * 2;
    if (*p++ != ':' || get_hex(p, want, PASS_HASH_BYTES) < 0 || p[PASS_HASH_BYTES * 2] != 0)
        return 0;
    pbkdf2(password, salt, iterations, got);
    for (uint32_t i = 0; i < PASS_HASH_BYTES; i++)
        diff |= got[i] ^ want[i]; /* no early exit: timing says nothing */
    return diff == 0;
}
