/* RSA and ECDSA signature verification over Montgomery arithmetic. */
#include "bs.h"
#include "crypto.h"

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

#define MAX_LIMBS 132
#define EC_LIMBS 14
#define MIN_RSA_BYTES 256u  /* 2048 bits */
#define MAX_RSA_BYTES 512u  /* 4096 bits */

struct mont {
    int n;
    u32 m[MAX_LIMBS], one[MAX_LIMBS], rr[MAX_LIMBS];
    u32 n0;
};

/* ---- Big integers (little-endian limbs) -------------------------------- */

static void from_bytes(u32 *out, int n, const u8 *bytes, u32 length)
{
    memset(out, 0, (unsigned int)n * 4);
    for (u32 i = 0; i < length; i++) {
        u32 position = length - 1 - i;

        if ((int)(i / 4) < n)
            out[i / 4] |= (u32)bytes[position] << (i % 4 * 8);
    }
}

static void to_bytes(u8 *out, u32 length, const u32 *value, int n)
{
    for (u32 i = 0; i < length; i++)
        out[length - 1 - i] = (int)(i / 4) < n ? (u8)(value[i / 4] >> (i % 4 * 8)) : 0;
}

static int compare(const u32 *a, const u32 *b, int n)
{
    for (int i = n - 1; i >= 0; i--)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

static int is_zero(const u32 *a, int n)
{
    for (int i = 0; i < n; i++)
        if (a[i])
            return 0;
    return 1;
}

static u32 subtract(u32 *out, const u32 *a, const u32 *b, int n)
{
    u64 borrow = 0;

    for (int i = 0; i < n; i++) {
        u64 d = (u64)a[i] - b[i] - borrow;

        out[i] = (u32)d;
        borrow = d >> 63;
    }
    return (u32)borrow;
}

static u32 add(u32 *out, const u32 *a, const u32 *b, int n)
{
    u64 carry = 0;

    for (int i = 0; i < n; i++) {
        u64 s = (u64)a[i] + b[i] + carry;

        out[i] = (u32)s;
        carry = s >> 32;
    }
    return (u32)carry;
}

static void mod_add(const struct mont *c, u32 *out, const u32 *a, const u32 *b)
{
    if (add(out, a, b, c->n) || compare(out, c->m, c->n) >= 0)
        subtract(out, out, c->m, c->n);
}

static void mod_sub(const struct mont *c, u32 *out, const u32 *a, const u32 *b)
{
    if (subtract(out, a, b, c->n))
        add(out, out, c->m, c->n);
}

static void mont_mul(const struct mont *c, u32 *out, const u32 *a, const u32 *b)
{
    u32 t[MAX_LIMBS + 2];
    int n = c->n;

    memset(t, 0, (unsigned int)(n + 2) * 4);
    for (int i = 0; i < n; i++) {
        u64 carry = 0, sum;
        u32 factor;

        for (int j = 0; j < n; j++) {
            sum = (u64)t[j] + (u64)a[j] * b[i] + carry;
            t[j] = (u32)sum;
            carry = sum >> 32;
        }
        sum = (u64)t[n] + carry;
        t[n] = (u32)sum;
        t[n + 1] = (u32)(sum >> 32);

        factor = t[0] * c->n0;
        sum = (u64)t[0] + (u64)factor * c->m[0];
        carry = sum >> 32;
        for (int j = 1; j < n; j++) {
            sum = (u64)t[j] + (u64)factor * c->m[j] + carry;
            t[j - 1] = (u32)sum;
            carry = sum >> 32;
        }
        sum = (u64)t[n] + carry;
        t[n - 1] = (u32)sum;
        t[n] = t[n + 1] + (u32)(sum >> 32);
    }
    if (t[n] || compare(t, c->m, n) >= 0)
        subtract(t, t, c->m, n);
    memcpy(out, t, (unsigned int)n * 4);
}

static void double_mod(const struct mont *c, u32 *a)
{
    if (add(a, a, a, c->n) || compare(a, c->m, c->n) >= 0)
        subtract(a, a, c->m, c->n);
}

static int mont_init(struct mont *c, const u8 *modulus, u32 length)
{
    u32 x = 1;

    c->n = (int)((length + 3) / 4);
    if (c->n > MAX_LIMBS || c->n == 0)
        return -1;
    from_bytes(c->m, c->n, modulus, length);
    if (!(c->m[0] & 1))
        return -1;
    for (int i = 0; i < 5; i++)
        x *= 2 - c->m[0] * x;
    c->n0 = 0 - x;

    memset(c->one, 0, sizeof(c->one));
    c->one[0] = 1;
    if (compare(c->one, c->m, c->n) >= 0)
        return -1;
    for (int i = 0; i < c->n * 32; i++)
        double_mod(c, c->one);
    memcpy(c->rr, c->one, sizeof(c->rr));
    for (int i = 0; i < c->n * 32; i++)
        double_mod(c, c->rr);
    return 0;
}

static void to_mont(const struct mont *c, u32 *out, const u32 *a)
{
    mont_mul(c, out, a, c->rr);
}

static void from_mont(const struct mont *c, u32 *out, const u32 *a)
{
    u32 unit[MAX_LIMBS];

    memset(unit, 0, sizeof(unit));
    unit[0] = 1;
    mont_mul(c, out, a, unit);
}

/* out = base^exponent, with base and out in Montgomery form. */
static void mont_pow(const struct mont *c, u32 *out, const u32 *base, const u32 *exponent, int exponent_limbs)
{
    u32 result[MAX_LIMBS];

    memcpy(result, c->one, (unsigned int)c->n * 4);
    for (int bit = exponent_limbs * 32 - 1; bit >= 0; bit--) {
        mont_mul(c, result, result, result);
        if (exponent[bit / 32] >> (bit % 32) & 1)
            mont_mul(c, result, result, base);
    }
    memcpy(out, result, (unsigned int)c->n * 4);
}

/* a^(m-2) mod m, for prime m. Both in Montgomery form. */
static void mont_invert(const struct mont *c, u32 *out, const u32 *a)
{
    u32 exponent[MAX_LIMBS], two[MAX_LIMBS];

    memset(two, 0, sizeof(two));
    two[0] = 2;
    subtract(exponent, c->m, two, c->n);
    mont_pow(c, out, a, exponent, c->n);
}

/* ---- RSA ---------------------------------------------------------------- */

static struct mont rsa;

/* Computes signature^e mod n into em, which has n_length bytes. */
static int rsa_public(const u8 *n, u32 n_length, const u8 *e, u32 e_length, const u8 *signature,
                      u32 signature_length, u8 *em)
{
    u32 s[MAX_LIMBS], base[MAX_LIMBS], exponent[MAX_LIMBS], result[MAX_LIMBS];

    while (n_length && n[0] == 0) {
        n++;
        n_length--;
    }
    if (n_length < MIN_RSA_BYTES || n_length > MAX_RSA_BYTES || signature_length != n_length ||
        e_length == 0 || e_length > 8)
        return -1;
    if (mont_init(&rsa, n, n_length) < 0)
        return -1;

    from_bytes(s, rsa.n, signature, signature_length);
    if (compare(s, rsa.m, rsa.n) >= 0)
        return -1;
    from_bytes(exponent, rsa.n, e, e_length);
    to_mont(&rsa, base, s);
    mont_pow(&rsa, result, base, exponent, rsa.n);
    from_mont(&rsa, result, result);
    to_bytes(em, n_length, result, rsa.n);
    return (int)n_length;
}

static const u8 *digest_info(u32 hash, u32 *length)
{
    static const u8 sha256_info[] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
                                     0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
    static const u8 sha384_info[] = {0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
                                     0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30};
    static const u8 sha512_info[] = {0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
                                     0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40};

    *length = sizeof(sha256_info);
    return hash == HASH_SHA256 ? sha256_info : hash == HASH_SHA384 ? sha384_info : sha512_info;
}

int rsa_verify_pkcs1(const uint8_t *n, uint32_t n_length, const uint8_t *e, uint32_t e_length, uint32_t hash,
                     const uint8_t *digest, const uint8_t *signature, uint32_t signature_length)
{
    u8 em[MAX_RSA_BYTES];
    u32 info_length, pad_end, difference = 0;
    const u8 *info = digest_info(hash, &info_length);
    int length = rsa_public(n, n_length, e, e_length, signature, signature_length, em);

    if (length < 0 || (u32)length < info_length + hash + 11)
        return -1;
    pad_end = (u32)length - info_length - hash - 1;

    difference |= em[0] ^ 0x00;
    difference |= em[1] ^ 0x01;
    for (u32 i = 2; i < pad_end; i++)
        difference |= em[i] ^ 0xFF;
    difference |= em[pad_end];
    for (u32 i = 0; i < info_length; i++)
        difference |= em[pad_end + 1 + i] ^ info[i];
    for (u32 i = 0; i < hash; i++)
        difference |= em[pad_end + 1 + info_length + i] ^ digest[i];
    return difference ? -1 : 0;
}

static void mgf1_xor(u8 *target, u32 target_length, const u8 *seed, u32 seed_length, u32 hash)
{
    u8 input[HASH_MAX + 4], output[HASH_MAX];
    u32 counter = 0, offset = 0;

    memcpy(input, seed, seed_length);
    while (offset < target_length) {
        input[seed_length] = (u8)(counter >> 24);
        input[seed_length + 1] = (u8)(counter >> 16);
        input[seed_length + 2] = (u8)(counter >> 8);
        input[seed_length + 3] = (u8)counter;
        hash_digest(hash, input, seed_length + 4, output);
        for (u32 i = 0; i < hash && offset < target_length; i++, offset++)
            target[offset] ^= output[i];
        counter++;
    }
}

int rsa_verify_pss(const uint8_t *n, uint32_t n_length, const uint8_t *e, uint32_t e_length, uint32_t hash,
                   const uint8_t *digest, const uint8_t *signature, uint32_t signature_length)
{
    static u8 message[8 + HASH_MAX * 2];
    u8 em_buffer[MAX_RSA_BYTES], computed[HASH_MAX], *em = em_buffer;
    int length = rsa_public(n, n_length, e, e_length, signature, signature_length, em);
    u32 em_length, db_length, salt = hash, top_bits, difference = 0;
    u32 limb;

    if (length < 0)
        return -1;
    em_length = (u32)length;

    /* emBits is one less than the modulus size. */
    limb = rsa.m[rsa.n - 1];
    top_bits = 0;
    while (limb) {
        top_bits++;
        limb >>= 1;
    }
    top_bits = (top_bits + 7) % 8; /* bits of em[0] that may be set after masking one off */
    if (top_bits == 0) {
        if (em[0])
            return -1;
        em++;
        em_length--;
        top_bits = 8;
    }
    if (em_length < hash + salt + 2 || em[em_length - 1] != 0xBC)
        return -1;
    db_length = em_length - hash - 1;

    mgf1_xor(em, db_length, em + db_length, hash, hash);
    em[0] &= (u8)(0xFF >> (8 - top_bits));

    for (u32 i = 0; i < db_length - salt - 1; i++)
        difference |= em[i];
    difference |= em[db_length - salt - 1] ^ 0x01;

    memset(message, 0, 8);
    memcpy(message + 8, digest, hash);
    memcpy(message + 8 + hash, em + db_length - salt, salt);
    hash_digest(hash, message, 8 + hash + salt, computed);
    for (u32 i = 0; i < hash; i++)
        difference |= computed[i] ^ em[db_length + i];
    return difference ? -1 : 0;
}

/* ---- Elliptic curves ------------------------------------------------------ */

struct curve {
    u32 bytes;
    struct mont p, order;
    u32 b[EC_LIMBS], gx[EC_LIMBS], gy[EC_LIMBS];
};

struct point {
    u32 x[EC_LIMBS], y[EC_LIMBS], z[EC_LIMBS];
};

static const struct {
    u32 bytes;
    const char *p, *b, *n, *gx, *gy;
} curve_params[] = {
    [EC_P256] = {32,
        "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF",
        "5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B",
        "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551",
        "6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296",
        "4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5"},
    [EC_P384] = {48,
        "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFFFF0000000000000000FFFFFFFF",
        "B3312FA7E23EE7E4988E056BE3F82D19181D9C6EFE8141120314088F5013875AC656398D8A2ED19D2A85C8EDD3EC2AEF",
        "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC7634D81F4372DDF581A0DB248B0A77AECEC196ACCC52973",
        "AA87CA22BE8B05378EB1C71EF320AD746E1D3B628BA79B9859F741E082542A385502F25DBF55296C3A545E3872760AB7",
        "3617DE4A96262C6F5D9E98BF9292DC29F8F41DBD289A147CE9DA3113B5F0B8C00A60B1CE1D7E819D7A431D7C90EA0E5F"},
};

static struct curve curves[3];

static int hex_digit(char c)
{
    return c <= '9' ? c - '0' : c - 'A' + 10;
}

static void hex_to_bytes(u8 *out, const char *hex, u32 bytes)
{
    for (u32 i = 0; i < bytes; i++)
        out[i] = (u8)(hex_digit(hex[i * 2]) << 4 | hex_digit(hex[i * 2 + 1]));
}

static void load_field(const struct curve *c, u32 *out, const char *hex)
{
    u8 bytes[48];
    u32 plain[EC_LIMBS];

    hex_to_bytes(bytes, hex, c->bytes);
    from_bytes(plain, c->p.n, bytes, c->bytes);
    to_mont(&c->p, out, plain);
}

static struct curve *get_curve(enum ec_curve id)
{
    struct curve *c = &curves[id];
    u8 bytes[48];

    if (c->bytes)
        return c;
    c->bytes = curve_params[id].bytes;
    hex_to_bytes(bytes, curve_params[id].p, c->bytes);
    mont_init(&c->p, bytes, c->bytes);
    hex_to_bytes(bytes, curve_params[id].n, c->bytes);
    mont_init(&c->order, bytes, c->bytes);
    load_field(c, c->b, curve_params[id].b);
    load_field(c, c->gx, curve_params[id].gx);
    load_field(c, c->gy, curve_params[id].gy);
    return c;
}

static void point_double(const struct curve *curve, struct point *r, const struct point *a)
{
    const struct mont *p = &curve->p;
    u32 delta[EC_LIMBS], gamma[EC_LIMBS], beta[EC_LIMBS], alpha[EC_LIMBS];
    u32 t1[EC_LIMBS], t2[EC_LIMBS], x3[EC_LIMBS], y3[EC_LIMBS], z3[EC_LIMBS];

    mont_mul(p, delta, a->z, a->z);
    mont_mul(p, gamma, a->y, a->y);
    mont_mul(p, beta, a->x, gamma);

    mod_sub(p, t1, a->x, delta);
    mod_add(p, t2, a->x, delta);
    mont_mul(p, alpha, t1, t2);
    mod_add(p, t1, alpha, alpha);
    mod_add(p, alpha, t1, alpha);

    mont_mul(p, x3, alpha, alpha);
    mod_add(p, t1, beta, beta);
    mod_add(p, t1, t1, t1);          /* 4 beta */
    mod_add(p, t2, t1, t1);          /* 8 beta */
    mod_sub(p, x3, x3, t2);

    mod_add(p, z3, a->y, a->z);
    mont_mul(p, z3, z3, z3);
    mod_sub(p, z3, z3, gamma);
    mod_sub(p, z3, z3, delta);

    mod_sub(p, t1, t1, x3);
    mont_mul(p, y3, alpha, t1);
    mont_mul(p, t2, gamma, gamma);
    mod_add(p, t2, t2, t2);
    mod_add(p, t2, t2, t2);
    mod_add(p, t2, t2, t2);          /* 8 gamma^2 */
    mod_sub(p, y3, y3, t2);

    memcpy(r->x, x3, sizeof(x3));
    memcpy(r->y, y3, sizeof(y3));
    memcpy(r->z, z3, sizeof(z3));
}

static void point_add(const struct curve *curve, struct point *r, const struct point *a, const struct point *b)
{
    const struct mont *p = &curve->p;
    u32 z1z1[EC_LIMBS], z2z2[EC_LIMBS], u1[EC_LIMBS], u2[EC_LIMBS], s1[EC_LIMBS], s2[EC_LIMBS];
    u32 h[EC_LIMBS], rr[EC_LIMBS], hh[EC_LIMBS], hhh[EC_LIMBS], v[EC_LIMBS], t[EC_LIMBS];
    u32 x3[EC_LIMBS], y3[EC_LIMBS], z3[EC_LIMBS];

    if (is_zero(a->z, p->n)) {
        *r = *b;
        return;
    }
    if (is_zero(b->z, p->n)) {
        *r = *a;
        return;
    }

    mont_mul(p, z1z1, a->z, a->z);
    mont_mul(p, z2z2, b->z, b->z);
    mont_mul(p, u1, a->x, z2z2);
    mont_mul(p, u2, b->x, z1z1);
    mont_mul(p, s1, a->y, b->z);
    mont_mul(p, s1, s1, z2z2);
    mont_mul(p, s2, b->y, a->z);
    mont_mul(p, s2, s2, z1z1);
    mod_sub(p, h, u2, u1);
    mod_sub(p, rr, s2, s1);

    if (is_zero(h, p->n)) {
        if (is_zero(rr, p->n)) {
            point_double(curve, r, a);
        } else {
            memset(r, 0, sizeof(*r)); /* the point at infinity */
        }
        return;
    }

    mont_mul(p, hh, h, h);
    mont_mul(p, hhh, hh, h);
    mont_mul(p, v, u1, hh);

    mont_mul(p, x3, rr, rr);
    mod_sub(p, x3, x3, hhh);
    mod_add(p, t, v, v);
    mod_sub(p, x3, x3, t);

    mod_sub(p, t, v, x3);
    mont_mul(p, y3, rr, t);
    mont_mul(p, t, s1, hhh);
    mod_sub(p, y3, y3, t);

    mont_mul(p, z3, a->z, b->z);
    mont_mul(p, z3, z3, h);

    memcpy(r->x, x3, sizeof(x3));
    memcpy(r->y, y3, sizeof(y3));
    memcpy(r->z, z3, sizeof(z3));
}

static int on_curve(const struct curve *c, const u32 *x, const u32 *y)
{
    const struct mont *p = &c->p;
    u32 left[EC_LIMBS], right[EC_LIMBS], t[EC_LIMBS];

    mont_mul(p, left, y, y);
    mont_mul(p, right, x, x);
    mont_mul(p, right, right, x);
    mod_add(p, t, x, x);
    mod_add(p, t, t, x);
    mod_sub(p, right, right, t);
    mod_add(p, right, right, c->b);
    return compare(left, right, p->n) == 0;
}

/* Reads a DER INTEGER into a fixed-width big-endian buffer. */
static const u8 *read_integer(const u8 *p, const u8 *end, u8 *out, u32 width)
{
    u32 length;

    if (end - p < 2 || p[0] != 0x02 || p[1] >= 0x80)
        return 0;
    length = p[1];
    p += 2;
    if ((u32)(end - p) < length || length == 0)
        return 0;
    while (length > 1 && p[0] == 0) {
        p++;
        length--;
    }
    if (length > width)
        return 0;
    memset(out, 0, width);
    memcpy(out + width - length, p, length);
    return p + length;
}

int ecdsa_verify(enum ec_curve id, const uint8_t *point, uint32_t point_length, const uint8_t *digest,
                 uint32_t digest_length, const uint8_t *signature, uint32_t signature_length)
{
    struct curve *c;
    const struct mont *order, *field;
    const u8 *p = signature, *end = signature + signature_length;
    u8 r_bytes[48], s_bytes[48], hash_bytes[48];
    u32 r[EC_LIMBS], s[EC_LIMBS], e[EC_LIMBS], inverse[EC_LIMBS], t[EC_LIMBS];
    u32 u1[EC_LIMBS], u2[EC_LIMBS], plain_x[EC_LIMBS], plain_y[EC_LIMBS];
    struct point q, g, sum, result;
    u32 take;

    if (id != EC_P256 && id != EC_P384)
        return -1;
    c = get_curve(id);
    order = &c->order;
    field = &c->p;

    if (point_length != 1 + 2 * c->bytes || point[0] != 0x04)
        return -1;

    /* The signature: SEQUENCE { INTEGER r, INTEGER s } */
    if (end - p < 2 || p[0] != 0x30)
        return -1;
    if (p[1] == 0x81)
        p += 3;
    else if (p[1] < 0x80)
        p += 2;
    else
        return -1;
    p = read_integer(p, end, r_bytes, c->bytes);
    if (!p)
        return -1;
    p = read_integer(p, end, s_bytes, c->bytes);
    if (!p)
        return -1;

    from_bytes(r, order->n, r_bytes, c->bytes);
    from_bytes(s, order->n, s_bytes, c->bytes);
    if (is_zero(r, order->n) || is_zero(s, order->n) || compare(r, order->m, order->n) >= 0 ||
        compare(s, order->m, order->n) >= 0)
        return -1;

    /* The public key must be a real point on this curve. */
    from_bytes(plain_x, field->n, point + 1, c->bytes);
    from_bytes(plain_y, field->n, point + 1 + c->bytes, c->bytes);
    if (compare(plain_x, field->m, field->n) >= 0 || compare(plain_y, field->m, field->n) >= 0)
        return -1;
    to_mont(field, q.x, plain_x);
    to_mont(field, q.y, plain_y);
    memcpy(q.z, field->one, (unsigned int)field->n * 4);
    if (!on_curve(c, q.x, q.y))
        return -1;

    /* e: the leftmost bytes of the digest, reduced modulo the order. */
    take = digest_length < c->bytes ? digest_length : c->bytes;
    memset(hash_bytes, 0, sizeof(hash_bytes));
    memcpy(hash_bytes + c->bytes - take, digest, take);
    from_bytes(e, order->n, hash_bytes, c->bytes);
    if (compare(e, order->m, order->n) >= 0)
        subtract(e, e, order->m, order->n);

    /* u1 = e/s, u2 = r/s (mod n) */
    to_mont(order, t, s);
    mont_invert(order, inverse, t);
    {
        u32 e_mont[EC_LIMBS], r_mont[EC_LIMBS];

        to_mont(order, e_mont, e);
        to_mont(order, r_mont, r);
        mont_mul(order, u1, e_mont, inverse);
        mont_mul(order, u2, r_mont, inverse);
        from_mont(order, u1, u1);
        from_mont(order, u2, u2);
    }

    /* R = u1*G + u2*Q, computed together. */
    memcpy(g.x, c->gx, sizeof(g.x));
    memcpy(g.y, c->gy, sizeof(g.y));
    memcpy(g.z, field->one, (unsigned int)field->n * 4);
    point_add(c, &sum, &g, &q);
    memset(&result, 0, sizeof(result));

    for (int bit = (int)c->bytes * 8 - 1; bit >= 0; bit--) {
        int a = u1[bit / 32] >> (bit % 32) & 1;
        int b = u2[bit / 32] >> (bit % 32) & 1;

        if (!is_zero(result.z, field->n))
            point_double(c, &result, &result);
        if (a && b)
            point_add(c, &result, &result, &sum);
        else if (a)
            point_add(c, &result, &result, &g);
        else if (b)
            point_add(c, &result, &result, &q);
    }

    if (is_zero(result.z, field->n))
        return -1;

    /* R.x (affine), reduced modulo the order, must equal r. */
    mont_invert(field, inverse, result.z);
    mont_mul(field, t, inverse, inverse);
    mont_mul(field, t, t, result.x);
    from_mont(field, t, t);
    if (compare(t, order->m, order->n) >= 0)
        subtract(t, t, order->m, order->n);
    return compare(t, r, order->n) == 0 ? 0 : -1;
}
