/* Hashes, public-key signature checks and X.509, used by TLS. */
#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>

/* SHA-2. Each algorithm is named by its digest size in bytes. */
#define HASH_SHA256 32u
#define HASH_SHA384 48u
#define HASH_SHA512 64u
#define HASH_MAX 64u

struct sha256 {
    uint32_t h[8];
    uint8_t block[64];
    uint32_t used;
    uint64_t total;
};

void sha_init(struct sha256 *s);
void sha_update(struct sha256 *s, const void *data, uint32_t length);
/* Leaves *s unusable; copy it first to keep hashing. */
void sha_final(struct sha256 *s, uint8_t *out);
void hash_digest(uint32_t algorithm, const void *data, uint32_t length, uint8_t *out);

/* Signature checks; all return 0 if the signature is valid. */
int rsa_verify_pkcs1(const uint8_t *n, uint32_t n_length, const uint8_t *e, uint32_t e_length,
                     uint32_t hash, const uint8_t *digest, const uint8_t *signature, uint32_t signature_length);
int rsa_verify_pss(const uint8_t *n, uint32_t n_length, const uint8_t *e, uint32_t e_length,
                   uint32_t hash, const uint8_t *digest, const uint8_t *signature, uint32_t signature_length);

enum ec_curve { EC_NONE, EC_P256, EC_P384 };
/* point is an uncompressed X9.62 point; signature is DER. */
int ecdsa_verify(enum ec_curve curve, const uint8_t *point, uint32_t point_length, const uint8_t *digest,
                 uint32_t digest_length, const uint8_t *signature, uint32_t signature_length);

/*
 * Verifies a TLS Certificate message body against /conf/roots.der and host.
 * Returns 0, or a negative value with *reason set.
 */
int x509_verify_chain(const uint8_t *message, uint32_t length, const char *host, const char **reason);
/* Checks a TLS signature made by the leaf of the last verified chain. */
int x509_verify_leaf_signature(uint32_t scheme, const uint8_t *data, uint32_t data_length,
                               const uint8_t *signature, uint32_t signature_length);

#endif
