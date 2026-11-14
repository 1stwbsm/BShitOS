/* TLS 1.2 client. See tls.h for what it does and does not guarantee. */
#include "bs.h"
#include "crypto.h"
#include "tls.h"

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long i64;

#define HASH_SIZE 32u
#define KEY_SIZE 16u
#define SALT_SIZE 4u
#define EXPLICIT_NONCE 8u
#define TAG_SIZE 16u
#define RANDOM_SIZE 32u
#define X25519_SIZE 32u
#define MASTER_SIZE 48u
#define VERIFY_SIZE 12u
#define MAX_FRAGMENT 16384u
#define MAX_RECORD (MAX_FRAGMENT + 2048u)
#define HANDSHAKE_BUFFER 65536u
#define HELLO_MAX 512u
#define PRF_SEED_MAX 128u

#define RECORD_CCS 20
#define RECORD_ALERT 21
#define RECORD_HANDSHAKE 22
#define RECORD_DATA 23

#define HS_SERVER_HELLO 2
#define HS_CERTIFICATE 11
#define HS_KEY_EXCHANGE 12
#define HS_CERT_REQUEST 13
#define HS_HELLO_DONE 14
#define HS_CLIENT_KEY 16
#define HS_FINISHED 20

#define TLS_12 0x0303u
#define SUITE_RSA 0xC02Fu
#define SUITE_ECDSA 0xC02Bu
#define CURVE_NAMED 3
#define GROUP_X25519 29u
#define GROUP_P256 23u
#define ALERT_CLOSE_NOTIFY 0

static void hmac(const u8 *key, u32 key_length, const u8 *a, u32 a_length, const u8 *b, u32 b_length, u8 *out)
{
    struct sha256 s;
    u8 pad[64], inner[HASH_SIZE], reduced[HASH_SIZE];

    if (key_length > 64) {
        sha_init(&s);
        sha_update(&s, key, key_length);
        sha_final(&s, reduced);
        key = reduced;
        key_length = HASH_SIZE;
    }

    memset(pad, 0x36, sizeof(pad));
    for (u32 i = 0; i < key_length; i++)
        pad[i] ^= key[i];
    sha_init(&s);
    sha_update(&s, pad, sizeof(pad));
    sha_update(&s, a, a_length);
    sha_update(&s, b, b_length);
    sha_final(&s, inner);

    for (u32 i = 0; i < sizeof(pad); i++)
        pad[i] ^= 0x36 ^ 0x5C;
    sha_init(&s);
    sha_update(&s, pad, sizeof(pad));
    sha_update(&s, inner, HASH_SIZE);
    sha_final(&s, out);
}

/* The TLS 1.2 pseudo-random function: seed is label || seed1 || seed2. */
static void prf(const u8 *secret, u32 secret_length, const char *label, const u8 *seed1, u32 seed1_length,
                const u8 *seed2, u32 seed2_length, u8 *out, u32 out_length)
{
    u8 seed[PRF_SEED_MAX], a[HASH_SIZE], block[HASH_SIZE];
    u32 label_length = strlen(label), seed_length = label_length + seed1_length + seed2_length;

    memcpy(seed, label, label_length);
    memcpy(seed + label_length, seed1, seed1_length);
    memcpy(seed + label_length + seed1_length, seed2, seed2_length);

    hmac(secret, secret_length, seed, seed_length, 0, 0, a);
    while (out_length) {
        u32 chunk = out_length < HASH_SIZE ? out_length : HASH_SIZE;

        hmac(secret, secret_length, a, HASH_SIZE, seed, seed_length, block);
        memcpy(out, block, chunk);
        out += chunk;
        out_length -= chunk;
        hmac(secret, secret_length, a, HASH_SIZE, 0, 0, a);
    }
}

/* ---- X25519 (field arithmetic after TweetNaCl) ------------------------- */

typedef i64 field[16];

static void field_carry(field o)
{
    for (int i = 0; i < 16; i++) {
        i64 c;

        o[i] += (i64)1 << 16;
        c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

static void field_swap(field p, field q, int b)
{
    i64 mask = ~((i64)b - 1);

    for (int i = 0; i < 16; i++) {
        i64 t = mask & (p[i] ^ q[i]);

        p[i] ^= t;
        q[i] ^= t;
    }
}

static void field_pack(u8 *out, const field n)
{
    field t, m;

    memcpy(t, n, sizeof(field));
    field_carry(t);
    field_carry(t);
    field_carry(t);
    for (int j = 0; j < 2; j++) {
        int b;

        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        field_swap(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        out[2 * i] = (u8)t[i];
        out[2 * i + 1] = (u8)(t[i] >> 8);
    }
}

static void field_unpack(field o, const u8 *n)
{
    for (int i = 0; i < 16; i++)
        o[i] = n[2 * i] + ((i64)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

static void field_add(field o, const field a, const field b)
{
    for (int i = 0; i < 16; i++)
        o[i] = a[i] + b[i];
}

static void field_sub(field o, const field a, const field b)
{
    for (int i = 0; i < 16; i++)
        o[i] = a[i] - b[i];
}

static void field_mul(field o, const field a, const field b)
{
    i64 t[31];

    memset(t, 0, sizeof(t));
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++)
        t[i] += 38 * t[i + 16];
    memcpy(o, t, sizeof(field));
    field_carry(o);
    field_carry(o);
}

static void field_invert(field o, const field in)
{
    field c;

    memcpy(c, in, sizeof(field));
    for (int a = 253; a >= 0; a--) {
        field_mul(c, c, c);
        if (a != 2 && a != 4)
            field_mul(c, c, in);
    }
    memcpy(o, c, sizeof(field));
}

static void x25519(u8 *out, const u8 *scalar, const u8 *point)
{
    static const field k121665 = {0xDB41, 1};
    u8 z[X25519_SIZE];
    field x, a, b, c, d, e, f;

    memcpy(z, scalar, X25519_SIZE);
    z[31] = (z[31] & 127) | 64;
    z[0] &= 248;
    field_unpack(x, point);
    memset(a, 0, sizeof(a));
    memset(c, 0, sizeof(c));
    memset(d, 0, sizeof(d));
    memcpy(b, x, sizeof(field));
    a[0] = d[0] = 1;

    for (int i = 254; i >= 0; i--) {
        int r = (z[i >> 3] >> (i & 7)) & 1;

        field_swap(a, b, r);
        field_swap(c, d, r);
        field_add(e, a, c);
        field_sub(a, a, c);
        field_add(c, b, d);
        field_sub(b, b, d);
        field_mul(d, e, e);
        field_mul(f, a, a);
        field_mul(a, c, a);
        field_mul(c, b, e);
        field_add(e, a, c);
        field_sub(a, a, c);
        field_mul(b, a, a);
        field_sub(c, d, f);
        field_mul(a, c, k121665);
        field_add(a, a, d);
        field_mul(c, c, a);
        field_mul(a, d, f);
        field_mul(d, b, x);
        field_mul(b, e, e);
        field_swap(a, b, r);
        field_swap(c, d, r);
    }
    field_invert(c, c);
    field_mul(a, a, c);
    field_pack(out, a);
}

/* ---- AES-128-GCM -------------------------------------------------------- */

static u8 sbox[256];

static u8 rotl8(u8 v, int n)
{
    return (u8)(v << n | v >> (8 - n));
}

static void make_sbox(void)
{
    u8 p = 1, q = 1;

    if (sbox[0])
        return;
    do {
        p = (u8)(p ^ (u8)(p << 1) ^ (p & 0x80 ? 0x1B : 0));
        q ^= (u8)(q << 1);
        q ^= (u8)(q << 2);
        q ^= (u8)(q << 4);
        if (q & 0x80)
            q ^= 0x09;
        sbox[p] = (u8)(q ^ rotl8(q, 1) ^ rotl8(q, 2) ^ rotl8(q, 3) ^ rotl8(q, 4) ^ 0x63);
    } while (p != 1);
    sbox[0] = 0x63;
}

static u8 xtime(u8 v)
{
    return (u8)(v << 1 ^ (v & 0x80 ? 0x1B : 0));
}

struct gcm_key {
    u8 round_keys[176];
    u32 h[4];
};

static void aes_encrypt(const struct gcm_key *k, const u8 *in, u8 *out)
{
    u8 s[16];

    for (int i = 0; i < 16; i++)
        s[i] = in[i] ^ k->round_keys[i];

    for (int round = 1; round <= 10; round++) {
        u8 t[16];

        for (int column = 0; column < 4; column++)
            for (int row = 0; row < 4; row++)
                t[column * 4 + row] = sbox[s[((column + row) & 3) * 4 + row]];

        if (round < 10) {
            for (int column = 0; column < 4; column++) {
                u8 *c = t + column * 4;
                u8 a = c[0], b = c[1], d = c[2], e = c[3];
                u8 all = a ^ b ^ d ^ e;

                c[0] = a ^ all ^ xtime(a ^ b);
                c[1] = b ^ all ^ xtime(b ^ d);
                c[2] = d ^ all ^ xtime(d ^ e);
                c[3] = e ^ all ^ xtime(e ^ a);
            }
        }
        for (int i = 0; i < 16; i++)
            s[i] = t[i] ^ k->round_keys[round * 16 + i];
    }
    memcpy(out, s, 16);
}

static u32 load32(const u8 *p)
{
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static void store32(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static void gcm_init(struct gcm_key *k, const u8 *key)
{
    static const u8 zero[16];
    u8 hash[16], rcon = 1;

    make_sbox();
    memcpy(k->round_keys, key, 16);
    for (int i = 16; i < 176; i += 4) {
        u8 t[4];

        memcpy(t, k->round_keys + i - 4, 4);
        if (i % 16 == 0) {
            u8 first = t[0];

            t[0] = sbox[t[1]] ^ rcon;
            t[1] = sbox[t[2]];
            t[2] = sbox[t[3]];
            t[3] = sbox[first];
            rcon = xtime(rcon);
        }
        for (int j = 0; j < 4; j++)
            k->round_keys[i + j] = k->round_keys[i + j - 16] ^ t[j];
    }

    aes_encrypt(k, zero, hash);
    for (int i = 0; i < 4; i++)
        k->h[i] = load32(hash + i * 4);
}

static void ghash_block(const struct gcm_key *k, u8 *y, const u8 *block, u32 length)
{
    u32 x[4], v[4], z[4] = {0, 0, 0, 0};

    for (u32 i = 0; i < length; i++)
        y[i] ^= block[i];
    for (int i = 0; i < 4; i++) {
        x[i] = load32(y + i * 4);
        v[i] = k->h[i];
    }

    for (int i = 0; i < 128; i++) {
        u32 low = v[3] & 1;

        if (x[i >> 5] >> (31 - (i & 31)) & 1)
            for (int j = 0; j < 4; j++)
                z[j] ^= v[j];
        v[3] = v[3] >> 1 | v[2] << 31;
        v[2] = v[2] >> 1 | v[1] << 31;
        v[1] = v[1] >> 1 | v[0] << 31;
        v[0] >>= 1;
        if (low)
            v[0] ^= 0xE1000000u;
    }
    for (int i = 0; i < 4; i++)
        store32(y + i * 4, z[i]);
}

static void ghash(const struct gcm_key *k, u8 *y, const u8 *data, u32 length)
{
    while (length) {
        u32 chunk = length < 16 ? length : 16;

        ghash_block(k, y, data, chunk);
        data += chunk;
        length -= chunk;
    }
}

/* En- or decrypts in place, and computes the tag over the ciphertext. */
static void gcm_crypt(const struct gcm_key *k, const u8 *nonce, const u8 *aad, u32 aad_length, u8 *data,
                      u32 length, int encrypting, u8 *tag)
{
    u8 counter[16], stream[16], y[16], lengths[16], first[16];
    u32 block = 2;

    memcpy(counter, nonce, 12);
    store32(counter + 12, 1);
    aes_encrypt(k, counter, first);

    memset(y, 0, sizeof(y));
    ghash(k, y, aad, aad_length);
    if (!encrypting)
        ghash(k, y, data, length);

    for (u32 offset = 0; offset < length; offset += 16, block++) {
        u32 chunk = length - offset < 16 ? length - offset : 16;

        store32(counter + 12, block);
        aes_encrypt(k, counter, stream);
        for (u32 i = 0; i < chunk; i++)
            data[offset + i] ^= stream[i];
    }

    if (encrypting)
        ghash(k, y, data, length);

    memset(lengths, 0, sizeof(lengths));
    store32(lengths + 4, aad_length * 8);
    store32(lengths + 12, length * 8);
    ghash_block(k, y, lengths, 16);
    for (int i = 0; i < 16; i++)
        tag[i] = y[i] ^ first[i];
}

/* ---- Randomness --------------------------------------------------------- */

static int has_rdrand(void)
{
    unsigned int eax = 1, ecx = 0, edx = 0, ebx = 0;

    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    (void)ebx;
    (void)edx;
    (void)eax;
    return (ecx >> 30) & 1;
}

static u32 rdrand32(void)
{
    u32 value = 0;

    for (int attempt = 0; attempt < 16; attempt++) {
        unsigned char ok;

        __asm__ volatile(".byte 0x0F, 0xC7, 0xF0\n setc %1" : "=a"(value), "=q"(ok));
        if (ok)
            break;
    }
    return value;
}

static u64 rdtsc64(void)
{
    u32 low, high;

    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return (u64)high << 32 | low;
}

/* Hardware randomness where the CPU has it, otherwise timing jitter. */
static void random_bytes(u8 *out, u32 length)
{
    static u8 pool[HASH_SIZE];
    static u32 counter;
    int hardware = has_rdrand();
    struct sha256 s;
    struct bs_time now;

    sha_init(&s);
    sha_update(&s, pool, HASH_SIZE);
    get_time(&now);
    sha_update(&s, &now, sizeof(now));
    for (int i = 0; i < 64; i++) {
        u64 tick = rdtsc64();
        u32 value = hardware ? rdrand32() : 0;

        sha_update(&s, &tick, sizeof(tick));
        sha_update(&s, &value, sizeof(value));
        if (!hardware && i % 8 == 0)
            sleep_ms(1);
    }
    sha_final(&s, pool);

    while (length) {
        u8 block[HASH_SIZE];
        u32 chunk = length < HASH_SIZE ? length : HASH_SIZE;

        counter++;
        sha_init(&s);
        sha_update(&s, pool, HASH_SIZE);
        sha_update(&s, &counter, sizeof(counter));
        sha_final(&s, block);
        memcpy(out, block, chunk);
        out += chunk;
        length -= chunk;
    }
}

/* ---- Records ------------------------------------------------------------ */

static int connection = -1;
static const char *last_error = "";
static struct sha256 transcript;
static struct gcm_key write_key, read_key;
static u8 write_salt[SALT_SIZE], read_salt[SALT_SIZE];
static u64 write_sequence, read_sequence;
static int write_encrypted, read_encrypted;
static u8 wire[MAX_RECORD + 5];
static u8 outgoing[MAX_FRAGMENT + 5 + EXPLICIT_NONCE + TAG_SIZE];
static u8 *plain;
static u32 plain_length, plain_used;
static u8 handshake[HANDSHAKE_BUFFER];
static u32 handshake_have;
static int peer_closed;
static u32 consumed; /* bytes of the last message still in the handshake buffer */

static int fail(const char *reason)
{
    last_error = reason;
    return -BS_EIO;
}

const char *tls_error(void)
{
    return last_error;
}

static void put_sequence(u8 *out, u64 value)
{
    store32(out, (u32)(value >> 32));
    store32(out + 4, (u32)value);
}

static void make_aad(u8 *aad, u64 sequence, u8 type, u32 length)
{
    put_sequence(aad, sequence);
    aad[8] = type;
    aad[9] = TLS_12 >> 8;
    aad[10] = TLS_12 & 0xFF;
    aad[11] = (u8)(length >> 8);
    aad[12] = (u8)length;
}

static int send_all(const u8 *data, u32 length)
{
    int rc = tcp_send(connection, data, length);

    return rc < 0 ? fail("the connection failed while sending") : 0;
}

static int send_record(u8 type, const void *data, u32 length)
{
    u32 total = length;

    outgoing[0] = type;
    outgoing[1] = TLS_12 >> 8;
    outgoing[2] = TLS_12 & 0xFF;

    if (write_encrypted) {
        u8 nonce[12], aad[13];

        memcpy(nonce, write_salt, SALT_SIZE);
        put_sequence(nonce + SALT_SIZE, write_sequence);
        memcpy(outgoing + 5, nonce + SALT_SIZE, EXPLICIT_NONCE);
        memcpy(outgoing + 5 + EXPLICIT_NONCE, data, length);
        make_aad(aad, write_sequence, type, length);
        gcm_crypt(&write_key, nonce, aad, sizeof(aad), outgoing + 5 + EXPLICIT_NONCE, length, 1,
                  outgoing + 5 + EXPLICIT_NONCE + length);
        total = EXPLICIT_NONCE + length + TAG_SIZE;
        write_sequence++;
    } else {
        memcpy(outgoing + 5, data, length);
    }

    outgoing[3] = (u8)(total >> 8);
    outgoing[4] = (u8)total;
    return send_all(outgoing, 5 + total);
}

/* Returns 1 when filled, 0 if the peer closed first, negative on error. */
static int read_exact(u8 *out, u32 length)
{
    u32 got = 0;

    while (got < length) {
        int rc = tcp_receive(connection, out + got, length - got);

        if (rc < 0)
            return fail("the connection failed while receiving");
        if (rc == 0)
            return 0;
        got += (u32)rc;
    }
    return 1;
}

/* Reads one record into wire; plain/plain_length hold its contents. */
static int read_record(u8 *type)
{
    u32 length;
    int rc = read_exact(wire, 5);

    if (rc <= 0)
        return rc;
    length = (u32)wire[3] << 8 | wire[4];
    if (length > MAX_RECORD)
        return fail("the server sent an oversized record");
    rc = read_exact(wire + 5, length);
    if (rc <= 0)
        return rc < 0 ? rc : fail("the server hung up mid-record");

    *type = wire[0];
    plain = wire + 5;
    plain_length = length;
    plain_used = 0;

    if (read_encrypted) {
        u8 nonce[12], aad[13], tag[TAG_SIZE], difference = 0;
        u32 body;

        if (length < EXPLICIT_NONCE + TAG_SIZE)
            return fail("the server sent a truncated record");
        body = length - EXPLICIT_NONCE - TAG_SIZE;
        memcpy(nonce, read_salt, SALT_SIZE);
        memcpy(nonce + SALT_SIZE, plain, EXPLICIT_NONCE);
        make_aad(aad, read_sequence, *type, body);
        gcm_crypt(&read_key, nonce, aad, sizeof(aad), plain + EXPLICIT_NONCE, body, 0, tag);
        for (u32 i = 0; i < TAG_SIZE; i++)
            difference |= tag[i] ^ plain[EXPLICIT_NONCE + body + i];
        if (difference)
            return fail("a record failed its integrity check");
        plain += EXPLICIT_NONCE;
        plain_length = body;
        read_sequence++;
    }
    return 1;
}

/* ---- Handshake ---------------------------------------------------------- */

static int send_handshake(const u8 *message, u32 length)
{
    sha_update(&transcript, message, length);
    return send_record(RECORD_HANDSHAKE, message, length);
}

/* Returns the next handshake message; it stays valid until the next call. */
static int next_message(u8 *type, const u8 **body, u32 *length)
{
    if (consumed) {
        handshake_have -= consumed;
        for (u32 i = 0; i < handshake_have; i++)
            handshake[i] = handshake[i + consumed];
        consumed = 0;
    }

    for (;;) {
        if (handshake_have >= 4) {
            u32 size = (u32)handshake[1] << 16 | (u32)handshake[2] << 8 | handshake[3];

            if (size + 4 > HANDSHAKE_BUFFER)
                return fail("the server's handshake is too large");
            if (handshake_have >= size + 4) {
                *type = handshake[0];
                *body = handshake + 4;
                *length = size;
                consumed = size + 4;
                sha_update(&transcript, handshake, consumed);
                return 1;
            }
        }

        {
            u8 record_type = 0;
            int rc = read_record(&record_type);

            if (rc < 0)
                return rc;
            if (rc == 0)
                return fail("the server hung up during the handshake");
            if (record_type == RECORD_ALERT)
                return fail("the server refused the connection (TLS alert)");
            if (record_type != RECORD_HANDSHAKE)
                return fail("unexpected record during the handshake");
            if (handshake_have + plain_length > HANDSHAKE_BUFFER)
                return fail("the server's handshake is too large");
            memcpy(handshake + handshake_have, plain, plain_length);
            handshake_have += plain_length;
        }
    }
}

static void put16(u8 *p, u32 value)
{
    p[0] = (u8)(value >> 8);
    p[1] = (u8)value;
}

static u32 add_extension(u8 *p, u32 type, const u8 *data, u32 length)
{
    put16(p, type);
    put16(p + 2, length);
    memcpy(p + 4, data, length);
    return 4 + length;
}

/* Server names must not be IP addresses. */
static int is_ip_address(const char *host)
{
    for (; *host; host++)
        if ((*host < '0' || *host > '9') && *host != '.')
            return 0;
    return 1;
}

static int send_client_hello(const char *host, const u8 *client_random)
{
    /* P-256 is listed so servers will use P-256 certificates; X25519 stays first. */
    static const u8 groups[] = {0, 4, 0, GROUP_X25519, 0, GROUP_P256};
    static const u8 point_formats[] = {1, 0};
    static const u8 signatures[] = {0, 18, 4, 3, 8, 4, 4, 1, 5, 3, 8, 5, 5, 1, 6, 3, 8, 6, 6, 1};
    u8 hello[HELLO_MAX], name[300];
    u32 host_length = strlen(host), n = 4, extensions_at;

    if (host_length > 255)
        return fail("host name too long");

    put16(hello + n, TLS_12);
    n += 2;
    memcpy(hello + n, client_random, RANDOM_SIZE);
    n += RANDOM_SIZE;
    hello[n++] = 0; /* no session id */
    put16(hello + n, 4);
    put16(hello + n + 2, SUITE_RSA);
    put16(hello + n + 4, SUITE_ECDSA);
    n += 6;
    hello[n++] = 1; /* compression: none */
    hello[n++] = 0;

    extensions_at = n;
    n += 2;
    if (!is_ip_address(host)) {
        put16(name, host_length + 3);
        name[2] = 0;
        put16(name + 3, host_length);
        memcpy(name + 5, host, host_length);
        n += add_extension(hello + n, 0, name, host_length + 5);
    }
    n += add_extension(hello + n, 10, groups, sizeof(groups));
    n += add_extension(hello + n, 11, point_formats, sizeof(point_formats));
    n += add_extension(hello + n, 13, signatures, sizeof(signatures));
    put16(hello + extensions_at, n - extensions_at - 2);

    hello[0] = 1;
    hello[1] = 0;
    put16(hello + 2, n - 4);
    return send_handshake(hello, n);
}

static int parse_server_hello(const u8 *body, u32 length, u8 *server_random)
{
    u32 session_length, suite;

    if (length < 38)
        return fail("malformed server hello");
    if (((u32)body[0] << 8 | body[1]) != TLS_12)
        return fail("the server does not offer TLS 1.2");
    memcpy(server_random, body + 2, RANDOM_SIZE);
    session_length = body[34];
    if (35 + session_length + 3 > length)
        return fail("malformed server hello");
    suite = (u32)body[35 + session_length] << 8 | body[36 + session_length];
    if (suite != SUITE_RSA && suite != SUITE_ECDSA)
        return fail("the server chose an unsupported cipher");
    return 0;
}

/* Checks the server signed this key with its certificate's key. */
static int parse_key_exchange(const u8 *body, u32 length, u8 *server_public, const u8 *client_random,
                              const u8 *server_random)
{
    static u8 signed_data[2 * RANDOM_SIZE + 4 + X25519_SIZE];
    u32 scheme, signature_length;

    if (length < 8 + X25519_SIZE || body[0] != CURVE_NAMED ||
        ((u32)body[1] << 8 | body[2]) != GROUP_X25519 || body[3] != X25519_SIZE)
        return fail("the server did not use X25519");

    scheme = (u32)body[4 + X25519_SIZE] << 8 | body[5 + X25519_SIZE];
    signature_length = (u32)body[6 + X25519_SIZE] << 8 | body[7 + X25519_SIZE];
    if (8 + X25519_SIZE + signature_length != length)
        return fail("malformed key exchange");

    memcpy(signed_data, client_random, RANDOM_SIZE);
    memcpy(signed_data + RANDOM_SIZE, server_random, RANDOM_SIZE);
    memcpy(signed_data + 2 * RANDOM_SIZE, body, 4 + X25519_SIZE);
    if (x509_verify_leaf_signature(scheme, signed_data, sizeof(signed_data), body + 8 + X25519_SIZE,
                                   signature_length) < 0)
        return fail("the server's key exchange signature is invalid");

    memcpy(server_public, body + 4, X25519_SIZE);
    return 0;
}

static void transcript_hash(u8 *out)
{
    struct sha256 copy = transcript;

    sha_final(&copy, out);
}

static int finish_handshake(const u8 *premaster, const u8 *client_public, const u8 *client_random,
                            const u8 *server_random)
{
    u8 master[MASTER_SIZE], keys[2 * (KEY_SIZE + SALT_SIZE)], hash[HASH_SIZE];
    u8 key_message[4 + 1 + X25519_SIZE], finished[4 + VERIFY_SIZE], expected[VERIFY_SIZE];
    u8 change_cipher = 1, type = 0, record_type = 0, difference = 0;
    const u8 *body;
    u32 length;
    struct sha256 before;
    int rc;

    key_message[0] = HS_CLIENT_KEY;
    key_message[1] = 0;
    key_message[2] = 0;
    key_message[3] = 1 + X25519_SIZE;
    key_message[4] = X25519_SIZE;
    memcpy(key_message + 5, client_public, X25519_SIZE);
    rc = send_handshake(key_message, sizeof(key_message));
    if (rc < 0)
        return rc;

    prf(premaster, X25519_SIZE, "master secret", client_random, RANDOM_SIZE, server_random, RANDOM_SIZE,
        master, MASTER_SIZE);
    prf(master, MASTER_SIZE, "key expansion", server_random, RANDOM_SIZE, client_random, RANDOM_SIZE,
        keys, sizeof(keys));
    gcm_init(&write_key, keys);
    gcm_init(&read_key, keys + KEY_SIZE);
    memcpy(write_salt, keys + 2 * KEY_SIZE, SALT_SIZE);
    memcpy(read_salt, keys + 2 * KEY_SIZE + SALT_SIZE, SALT_SIZE);

    rc = send_record(RECORD_CCS, &change_cipher, 1);
    if (rc < 0)
        return rc;
    write_encrypted = 1;

    transcript_hash(hash);
    finished[0] = HS_FINISHED;
    finished[1] = 0;
    finished[2] = 0;
    finished[3] = VERIFY_SIZE;
    prf(master, MASTER_SIZE, "client finished", hash, HASH_SIZE, 0, 0, finished + 4, VERIFY_SIZE);
    rc = send_handshake(finished, sizeof(finished));
    if (rc < 0)
        return rc;

    /* The server's change-cipher record, then its Finished, encrypted. */
    rc = read_record(&record_type);
    if (rc < 0)
        return rc;
    if (rc == 0 || record_type == RECORD_ALERT)
        return fail("the server rejected the handshake");
    if (record_type != RECORD_CCS)
        return fail("unexpected record after the key exchange");
    read_encrypted = 1;

    before = transcript;
    rc = next_message(&type, &body, &length);
    if (rc < 0)
        return rc;
    if (type != HS_FINISHED || length != VERIFY_SIZE)
        return fail("the server's Finished message is malformed");

    {
        struct sha256 copy = before;

        sha_final(&copy, hash);
    }
    prf(master, MASTER_SIZE, "server finished", hash, HASH_SIZE, 0, 0, expected, VERIFY_SIZE);
    for (u32 i = 0; i < VERIFY_SIZE; i++)
        difference |= expected[i] ^ body[i];
    if (difference)
        return fail("the server's Finished message did not verify");
    plain_length = plain_used = 0; /* that record was handshake, not page data */
    return 0;
}

/* The chain keeps pointers into the certificates, so give them a stable home. */
static int x509_verify_chain_copy(const u8 *body, u32 length, const char *host, const char **reason)
{
    static u8 certificates[HANDSHAKE_BUFFER];

    if (length > HANDSHAKE_BUFFER) {
        *reason = "the server's certificates are too large";
        return -1;
    }
    memcpy(certificates, body, length);
    return x509_verify_chain(certificates, length, host, reason);
}

int tls_connect(int tcp_connection, const char *host)
{
    static const u8 base_point[X25519_SIZE] = {9};
    u8 client_random[RANDOM_SIZE], server_random[RANDOM_SIZE];
    u8 private_key[X25519_SIZE], client_public[X25519_SIZE], server_public[X25519_SIZE], premaster[X25519_SIZE];
    int have_key = 0, have_certificate = 0, rc;

    connection = tcp_connection;
    last_error = "";
    write_encrypted = read_encrypted = 0;
    write_sequence = read_sequence = 0;
    handshake_have = consumed = 0;
    plain_length = plain_used = 0;
    peer_closed = 0;
    sha_init(&transcript);

    random_bytes(client_random, RANDOM_SIZE);
    random_bytes(private_key, X25519_SIZE);
    x25519(client_public, private_key, base_point);

    rc = send_client_hello(host, client_random);
    if (rc < 0)
        return rc;

    for (;;) {
        u8 type = 0;
        const u8 *body;
        u32 length;

        rc = next_message(&type, &body, &length);
        if (rc < 0)
            return rc;

        if (type == HS_SERVER_HELLO) {
            rc = parse_server_hello(body, length, server_random);
        } else if (type == HS_CERTIFICATE) {
            const char *reason = "";

            rc = x509_verify_chain_copy(body, length, host, &reason);
            if (rc < 0)
                return fail(reason);
            have_certificate = 1;
        } else if (type == HS_KEY_EXCHANGE) {
            if (!have_certificate)
                return fail("the server did not send a certificate first");
            rc = parse_key_exchange(body, length, server_public, client_random, server_random);
            have_key = rc == 0;
        } else if (type == HS_CERT_REQUEST) {
            rc = fail("the server demands a client certificate");
        } else if (type == HS_HELLO_DONE) {
            break;
        } else {
            rc = 0; /* optional messages, such as certificate status */
        }
        if (rc < 0)
            return rc;
    }

    if (!have_key)
        return fail("the server did not send a key");

    x25519(premaster, private_key, server_public);
    {
        u8 any = 0;

        for (u32 i = 0; i < X25519_SIZE; i++)
            any |= premaster[i];
        if (!any)
            return fail("the server sent an invalid key");
    }
    return finish_handshake(premaster, client_public, client_random, server_random);
}

int tls_send(const void *data, unsigned int length)
{
    const u8 *p = data;
    unsigned int sent = 0;

    while (sent < length) {
        unsigned int chunk = length - sent < MAX_FRAGMENT ? length - sent : MAX_FRAGMENT;
        int rc = send_record(RECORD_DATA, p + sent, chunk);

        if (rc < 0)
            return rc;
        sent += chunk;
    }
    return (int)sent;
}

int tls_receive(void *buf, unsigned int size)
{
    while (plain_used >= plain_length) {
        u8 type = 0;
        int rc;

        if (peer_closed)
            return 0;
        rc = read_record(&type);
        if (rc < 0)
            return rc;
        if (rc == 0) {
            peer_closed = 1;
            return 0;
        }

        if (type == RECORD_ALERT) {
            plain_length = plain_used = 0;
            if (plain[1] == ALERT_CLOSE_NOTIFY) {
                peer_closed = 1;
                return 0;
            }
            return fail("the server ended the session with an error");
        }
        if (type != RECORD_DATA)
            plain_length = plain_used = 0; /* post-handshake messages are ignored */
    }

    {
        unsigned int available = plain_length - plain_used;
        unsigned int n = size < available ? size : available;

        memcpy(buf, plain + plain_used, n);
        plain_used += n;
        return (int)n;
    }
}

void tls_close(void)
{
    static const u8 close_notify[2] = {1, ALERT_CLOSE_NOTIFY};

    if (write_encrypted && !peer_closed)
        send_record(RECORD_ALERT, close_notify, sizeof(close_notify));
    write_encrypted = read_encrypted = 0;
}
