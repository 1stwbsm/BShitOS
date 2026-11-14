/* X.509 certificate parsing and chain verification. */
#include "bs.h"
#include "crypto.h"

typedef uint8_t u8;
typedef uint32_t u32;

#define ROOTS_PATH "/conf/roots.der"
#define ROOTS_MAX (640u * 1024u)
#define MAX_CHAIN 8
#define MAX_DEPTH 8
#define TIME_TEXT 14

#define TAG_BOOLEAN 0x01
#define TAG_INTEGER 0x02
#define TAG_BIT_STRING 0x03
#define TAG_OCTET_STRING 0x04
#define TAG_OID 0x06
#define TAG_UTC_TIME 0x17
#define TAG_GENERALIZED_TIME 0x18
#define TAG_SEQUENCE 0x30
#define TAG_SET 0x31
#define TAG_VERSION 0xA0
#define TAG_EXTENSIONS 0xA3
#define SAN_DNS 0x82
#define SAN_IP 0x87

enum key_kind { KEY_NONE, KEY_RSA, KEY_EC };
enum sig_kind { SIG_NONE, SIG_RSA_PKCS1, SIG_ECDSA };

struct tlv {
    u8 tag;
    const u8 *start, *body;
    u32 length, total;
};

struct cert {
    const u8 *tbs;
    u32 tbs_length;
    enum sig_kind sig;
    u32 sig_hash;
    const u8 *signature;
    u32 signature_length;
    const u8 *issuer, *subject;
    u32 issuer_length, subject_length;
    char not_before[TIME_TEXT + 1], not_after[TIME_TEXT + 1];
    enum key_kind key;
    const u8 *n, *e, *point;
    u32 n_length, e_length, point_length;
    enum ec_curve curve;
    int is_ca;
    const u8 *san;
    u32 san_length;
};

static const u8 OID_RSA_SHA256[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B};
static const u8 OID_RSA_SHA384[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0C};
static const u8 OID_RSA_SHA512[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0D};
static const u8 OID_ECDSA_SHA256[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02};
static const u8 OID_ECDSA_SHA384[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x03};
static const u8 OID_ECDSA_SHA512[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x04};
static const u8 OID_RSA_KEY[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
static const u8 OID_EC_KEY[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
static const u8 OID_P256[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
static const u8 OID_P384[] = {0x2B, 0x81, 0x04, 0x00, 0x22};
static const u8 OID_SAN[] = {0x55, 0x1D, 0x11};
static const u8 OID_BASIC_CONSTRAINTS[] = {0x55, 0x1D, 0x13};

static struct cert chain[MAX_CHAIN];
static int chain_length;
static u8 roots[ROOTS_MAX];
static int roots_length = -1;

/* ---- DER ---------------------------------------------------------------- */

static int der_read(const u8 *p, const u8 *end, struct tlv *out)
{
    u32 length, header = 2;

    if (end - p < 2)
        return -1;
    out->tag = p[0];
    out->start = p;
    length = p[1];
    if (length & 0x80) {
        u32 count = length & 0x7F;

        if (count == 0 || count > 3 || (u32)(end - p) < 2 + count)
            return -1;
        length = 0;
        for (u32 i = 0; i < count; i++)
            length = length << 8 | p[2 + i];
        header += count;
    }
    if ((u32)(end - p) < header || (u32)(end - p) - header < length)
        return -1;
    out->body = p + header;
    out->length = length;
    out->total = header + length;
    return 0;
}

static int expect(const u8 **p, const u8 *end, u8 tag, struct tlv *out)
{
    if (der_read(*p, end, out) < 0 || out->tag != tag)
        return -1;
    *p += out->total;
    return 0;
}

static int oid_is(const struct tlv *oid, const u8 *value, u32 length)
{
    if (oid->length != length)
        return 0;
    for (u32 i = 0; i < length; i++)
        if (oid->body[i] != value[i])
            return 0;
    return 1;
}

#define OID_IS(tlv, name) oid_is(tlv, name, sizeof(name))

/* ---- Parsing ------------------------------------------------------------- */

static void identify_signature(const struct tlv *oid, struct cert *c)
{
    c->sig = SIG_NONE;
    if (OID_IS(oid, OID_RSA_SHA256)) {
        c->sig = SIG_RSA_PKCS1;
        c->sig_hash = HASH_SHA256;
    } else if (OID_IS(oid, OID_RSA_SHA384)) {
        c->sig = SIG_RSA_PKCS1;
        c->sig_hash = HASH_SHA384;
    } else if (OID_IS(oid, OID_RSA_SHA512)) {
        c->sig = SIG_RSA_PKCS1;
        c->sig_hash = HASH_SHA512;
    } else if (OID_IS(oid, OID_ECDSA_SHA256)) {
        c->sig = SIG_ECDSA;
        c->sig_hash = HASH_SHA256;
    } else if (OID_IS(oid, OID_ECDSA_SHA384)) {
        c->sig = SIG_ECDSA;
        c->sig_hash = HASH_SHA384;
    } else if (OID_IS(oid, OID_ECDSA_SHA512)) {
        c->sig = SIG_ECDSA;
        c->sig_hash = HASH_SHA512;
    }
}

static int parse_time(const struct tlv *t, char *out)
{
    const u8 *p = t->body;
    u32 n = 0;

    if (t->tag == TAG_UTC_TIME && t->length == 13) {
        out[n++] = p[0] >= '5' ? '1' : '2';
        out[n++] = p[0] >= '5' ? '9' : '0';
        for (u32 i = 0; i < 12; i++)
            out[n++] = (char)p[i];
    } else if (t->tag == TAG_GENERALIZED_TIME && t->length == 15) {
        for (u32 i = 0; i < 14; i++)
            out[n++] = (char)p[i];
    } else {
        return -1;
    }
    out[n] = 0;
    return 0;
}

static int parse_key(const struct tlv *spki, struct cert *c)
{
    const u8 *p = spki->body, *end = spki->body + spki->length, *inner;
    struct tlv algorithm, oid, bits, sequence, n, e, curve;

    if (expect(&p, end, TAG_SEQUENCE, &algorithm) < 0)
        return -1;
    inner = algorithm.body;
    if (expect(&inner, algorithm.body + algorithm.length, TAG_OID, &oid) < 0)
        return -1;
    if (expect(&p, end, TAG_BIT_STRING, &bits) < 0 || bits.length < 2 || bits.body[0] != 0)
        return -1;

    if (OID_IS(&oid, OID_RSA_KEY)) {
        const u8 *key = bits.body + 1, *key_end = bits.body + bits.length, *fields;

        if (expect(&key, key_end, TAG_SEQUENCE, &sequence) < 0)
            return -1;
        fields = sequence.body;
        if (expect(&fields, sequence.body + sequence.length, TAG_INTEGER, &n) < 0 ||
            expect(&fields, sequence.body + sequence.length, TAG_INTEGER, &e) < 0)
            return -1;
        c->key = KEY_RSA;
        c->n = n.body;
        c->n_length = n.length;
        c->e = e.body;
        c->e_length = e.length;
    } else if (OID_IS(&oid, OID_EC_KEY)) {
        if (expect(&inner, algorithm.body + algorithm.length, TAG_OID, &curve) < 0)
            return -1;
        c->curve = OID_IS(&curve, OID_P256) ? EC_P256 : OID_IS(&curve, OID_P384) ? EC_P384 : EC_NONE;
        c->key = KEY_EC;
        c->point = bits.body + 1;
        c->point_length = bits.length - 1;
    } else {
        c->key = KEY_NONE;
    }
    return 0;
}

static void parse_extensions(const struct tlv *wrapper, struct cert *c)
{
    const u8 *p = wrapper->body, *end = wrapper->body + wrapper->length;
    struct tlv list;

    if (expect(&p, end, TAG_SEQUENCE, &list) < 0)
        return;
    p = list.body;
    end = list.body + list.length;

    while (p < end) {
        struct tlv extension, oid, value;
        const u8 *inner;

        if (expect(&p, end, TAG_SEQUENCE, &extension) < 0)
            return;
        inner = extension.body;
        if (expect(&inner, extension.body + extension.length, TAG_OID, &oid) < 0)
            return;
        if (der_read(inner, extension.body + extension.length, &value) == 0 && value.tag == TAG_BOOLEAN)
            inner += value.total;
        if (expect(&inner, extension.body + extension.length, TAG_OCTET_STRING, &value) < 0)
            return;

        if (OID_IS(&oid, OID_SAN)) {
            struct tlv names;
            const u8 *q = value.body;

            if (expect(&q, value.body + value.length, TAG_SEQUENCE, &names) == 0) {
                c->san = names.body;
                c->san_length = names.length;
            }
        } else if (OID_IS(&oid, OID_BASIC_CONSTRAINTS)) {
            struct tlv sequence, flag;
            const u8 *q = value.body;

            if (expect(&q, value.body + value.length, TAG_SEQUENCE, &sequence) == 0 && sequence.length > 0 &&
                der_read(sequence.body, sequence.body + sequence.length, &flag) == 0 &&
                flag.tag == TAG_BOOLEAN && flag.length == 1)
                c->is_ca = flag.body[0] != 0;
        }
    }
}

static int parse_cert(const u8 *der, u32 length, struct cert *c)
{
    const u8 *p = der, *end = der + length, *tbs_p, *tbs_end, *inner;
    struct tlv whole, tbs, algorithm, oid, signature, inner_alg, issuer, validity, subject, spki, start, stop, field;

    memset(c, 0, sizeof(*c));
    if (expect(&p, end, TAG_SEQUENCE, &whole) < 0)
        return -1;

    p = whole.body;
    end = whole.body + whole.length;
    if (expect(&p, end, TAG_SEQUENCE, &tbs) < 0 || expect(&p, end, TAG_SEQUENCE, &algorithm) < 0 ||
        expect(&p, end, TAG_BIT_STRING, &signature) < 0 || signature.length < 2 || signature.body[0] != 0)
        return -1;
    inner = algorithm.body;
    if (expect(&inner, algorithm.body + algorithm.length, TAG_OID, &oid) < 0)
        return -1;

    c->tbs = tbs.start;
    c->tbs_length = tbs.total;
    c->signature = signature.body + 1;
    c->signature_length = signature.length - 1;
    identify_signature(&oid, c);

    tbs_p = tbs.body;
    tbs_end = tbs.body + tbs.length;
    if (der_read(tbs_p, tbs_end, &field) == 0 && field.tag == TAG_VERSION)
        tbs_p += field.total;
    if (expect(&tbs_p, tbs_end, TAG_INTEGER, &field) < 0 || /* serial */
        expect(&tbs_p, tbs_end, TAG_SEQUENCE, &inner_alg) < 0 ||
        expect(&tbs_p, tbs_end, TAG_SEQUENCE, &issuer) < 0 ||
        expect(&tbs_p, tbs_end, TAG_SEQUENCE, &validity) < 0 ||
        expect(&tbs_p, tbs_end, TAG_SEQUENCE, &subject) < 0 ||
        expect(&tbs_p, tbs_end, TAG_SEQUENCE, &spki) < 0)
        return -1;

    /* The algorithm inside the signed part must match the outer one. */
    if (inner_alg.total != algorithm.total || memcmp_bytes(inner_alg.start, algorithm.start, algorithm.total))
        return -1;

    c->issuer = issuer.body;
    c->issuer_length = issuer.length;
    c->subject = subject.body;
    c->subject_length = subject.length;

    inner = validity.body;
    if (der_read(inner, validity.body + validity.length, &start) < 0)
        return -1;
    inner += start.total;
    if (der_read(inner, validity.body + validity.length, &stop) < 0)
        return -1;
    if (parse_time(&start, c->not_before) < 0 || parse_time(&stop, c->not_after) < 0)
        return -1;

    if (parse_key(&spki, c) < 0)
        return -1;

    while (tbs_p < tbs_end) {
        if (der_read(tbs_p, tbs_end, &field) < 0)
            return -1;
        if (field.tag == TAG_EXTENSIONS)
            parse_extensions(&field, c);
        tbs_p += field.total;
    }
    return 0;
}

/* ---- Policy --------------------------------------------------------------- */

static void digits(char *out, unsigned int value, int count)
{
    for (int i = count - 1; i >= 0; i--) {
        out[i] = (char)('0' + value % 10);
        value /= 10;
    }
}

static void current_time(char *out)
{
    struct bs_time t;

    get_time(&t);
    digits(out, t.year, 4);
    digits(out + 4, t.month, 2);
    digits(out + 6, t.day, 2);
    digits(out + 8, t.hour, 2);
    digits(out + 10, t.minute, 2);
    digits(out + 12, t.second, 2);
    out[TIME_TEXT] = 0;
}

static int time_valid(const struct cert *c, const char *now)
{
    return strcmp(now, c->not_before) >= 0 && strcmp(now, c->not_after) <= 0;
}

static int same_name(const u8 *a, u32 a_length, const u8 *b, u32 b_length)
{
    return a_length == b_length && memcmp_bytes(a, b, a_length) == 0;
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static int equal_ignoring_case(const char *a, u32 a_length, const char *b, u32 b_length)
{
    if (a_length != b_length)
        return 0;
    for (u32 i = 0; i < a_length; i++)
        if (lower(a[i]) != lower(b[i]))
            return 0;
    return 1;
}

/* "*.example.com" matches exactly one extra label, and only on the left. */
static int dns_matches(const char *pattern, u32 pattern_length, const char *host)
{
    u32 host_length = strlen(host);

    if (pattern_length > 2 && pattern[0] == '*' && pattern[1] == '.') {
        const char *dot = strchr(host, '.');

        if (!dot || dot == host)
            return 0;
        return equal_ignoring_case(pattern + 2, pattern_length - 2, dot + 1, host_length - (u32)(dot + 1 - host));
    }
    return equal_ignoring_case(pattern, pattern_length, host, host_length);
}

static int parse_ipv4(const char *host, u8 *out)
{
    u32 value = 0, parts = 0;
    int digit_count = 0;

    for (;; host++) {
        if (*host >= '0' && *host <= '9') {
            value = value * 10 + (u32)(*host - '0');
            digit_count++;
            if (value > 255)
                return 0;
        } else if ((*host == '.' || !*host) && digit_count && parts < 4) {
            out[parts++] = (u8)value;
            value = 0;
            digit_count = 0;
            if (!*host)
                return parts == 4;
        } else {
            return 0;
        }
    }
}

static int host_matches(const struct cert *c, const char *host)
{
    const u8 *p = c->san, *end = c->san + c->san_length;
    u8 ip[4];
    int host_is_ip = parse_ipv4(host, ip);

    while (c->san && p < end) {
        struct tlv name;

        if (der_read(p, end, &name) < 0)
            return 0;
        p += name.total;
        if (host_is_ip) {
            if (name.tag == SAN_IP && name.length == 4 && memcmp_bytes(name.body, ip, 4) == 0)
                return 1;
        } else if (name.tag == SAN_DNS && dns_matches((const char *)name.body, name.length, host)) {
            return 1;
        }
    }
    return 0;
}

/* Returns 0 if issuer's key signed child. */
static int signature_valid(const struct cert *child, const struct cert *issuer)
{
    u8 digest[HASH_MAX];

    if (child->sig == SIG_NONE)
        return -1;
    hash_digest(child->sig_hash, child->tbs, child->tbs_length, digest);

    if (child->sig == SIG_RSA_PKCS1 && issuer->key == KEY_RSA)
        return rsa_verify_pkcs1(issuer->n, issuer->n_length, issuer->e, issuer->e_length, child->sig_hash, digest,
                                child->signature, child->signature_length);
    if (child->sig == SIG_ECDSA && issuer->key == KEY_EC)
        return ecdsa_verify(issuer->curve, issuer->point, issuer->point_length, digest, child->sig_hash,
                            child->signature, child->signature_length);
    return -1;
}

static int load_roots(void)
{
    int fd;

    if (roots_length >= 0)
        return 0;
    fd = open(ROOTS_PATH, BS_O_READ);
    if (fd < 0)
        return -1;
    roots_length = read(fd, roots, ROOTS_MAX);
    close(fd);
    return roots_length > 0 ? 0 : -1;
}

/* Returns 1 if a trusted root issued c. */
static int issued_by_root(const struct cert *c, const char *now)
{
    static struct cert root;
    const u8 *p = roots, *end = roots + roots_length;

    while (p < end) {
        struct tlv whole;

        if (der_read(p, end, &whole) < 0)
            return 0;
        if (parse_cert(whole.start, whole.total, &root) == 0 &&
            same_name(root.subject, root.subject_length, c->issuer, c->issuer_length) && time_valid(&root, now) &&
            signature_valid(c, &root) == 0)
            return 1;
        p += whole.total;
    }
    return 0;
}

int x509_verify_chain(const uint8_t *message, uint32_t length, const char *host, const char **reason)
{
    const u8 *p = message + 3, *end;
    char now[TIME_TEXT + 1];
    int current = 0;

    chain_length = 0;
    if (length < 3 || ((u32)message[0] << 16 | (u32)message[1] << 8 | message[2]) != length - 3) {
        *reason = "the server's certificate list is malformed";
        return -1;
    }
    end = message + length;

    while (p < end && chain_length < MAX_CHAIN) {
        u32 size;

        if (end - p < 3) {
            *reason = "the server's certificate list is malformed";
            return -1;
        }
        size = (u32)p[0] << 16 | (u32)p[1] << 8 | p[2];
        p += 3;
        if ((u32)(end - p) < size || parse_cert(p, size, &chain[chain_length]) < 0) {
            *reason = "the server sent a certificate that could not be read";
            return -1;
        }
        chain_length++;
        p += size;
    }
    if (chain_length == 0) {
        *reason = "the server sent no certificate";
        return -1;
    }
    if (load_roots() < 0) {
        *reason = "the trusted root list /conf/roots.der is missing";
        return -1;
    }

    current_time(now);
    if (!host_matches(&chain[0], host)) {
        *reason = "the certificate is for a different site";
        return -1;
    }

    for (int depth = 0; depth < MAX_DEPTH; depth++) {
        const struct cert *c = &chain[current];
        int next = -1;

        if (!time_valid(c, now)) {
            *reason = "a certificate has expired or is not valid yet";
            return -1;
        }
        if (issued_by_root(c, now))
            return 0;

        for (int i = 0; i < chain_length && next < 0; i++) {
            if (i != current && chain[i].is_ca && same_name(chain[i].subject, chain[i].subject_length, c->issuer, c->issuer_length) &&
                signature_valid(c, &chain[i]) == 0)
                next = i;
        }
        if (next < 0) {
            *reason = "the certificate is not signed by a trusted authority";
            return -1;
        }
        current = next;
    }
    *reason = "the certificate chain is too long";
    return -1;
}

int x509_verify_leaf_signature(uint32_t scheme, const uint8_t *data, uint32_t data_length,
                               const uint8_t *signature, uint32_t signature_length)
{
    const struct cert *leaf = &chain[0];
    u8 digest[HASH_MAX];
    u32 hash;

    if (chain_length == 0)
        return -1;

    switch (scheme) {
    case 0x0401: case 0x0403: case 0x0804: hash = HASH_SHA256; break;
    case 0x0501: case 0x0503: case 0x0805: hash = HASH_SHA384; break;
    case 0x0601: case 0x0603: case 0x0806: hash = HASH_SHA512; break;
    default: return -1;
    }
    hash_digest(hash, data, data_length, digest);

    if ((scheme & 0xFF) == 0x01 && scheme != 0x0801 && leaf->key == KEY_RSA)
        return rsa_verify_pkcs1(leaf->n, leaf->n_length, leaf->e, leaf->e_length, hash, digest, signature, signature_length);
    if ((scheme >> 8) == 0x08 && leaf->key == KEY_RSA)
        return rsa_verify_pss(leaf->n, leaf->n_length, leaf->e, leaf->e_length, hash, digest, signature, signature_length);
    if ((scheme & 0xFF) == 0x03 && leaf->key == KEY_EC)
        return ecdsa_verify(leaf->curve, leaf->point, leaf->point_length, digest, hash, signature, signature_length);
    return -1;
}
