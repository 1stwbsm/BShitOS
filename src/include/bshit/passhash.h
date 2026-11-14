/*
 * Password hashing, shared by the kernel (which checks passwords) and userspace
 * (the installer writes the first ones). PBKDF2-HMAC-SHA256 with a random salt.
 *
 * A /conf/shadow line is "name:record". A record is "iterations:salt:hash" in
 * lowercase hex, "" for an account with an empty password, or "!" for a locked
 * account that cannot log in.
 */
#ifndef BSHIT_PASSHASH_H
#define BSHIT_PASSHASH_H

#include <stdint.h>

#define PASS_ITERATIONS 4096u
#define PASS_SALT_BYTES 16u
#define PASS_HASH_BYTES 32u
/* "4096:" + 32 hex + ":" + 64 hex + NUL, with room for bigger iteration counts */
#define PASS_RECORD_MAX 112u

struct pass_sha256 {
    uint32_t h[8];
    uint8_t block[64];
    uint32_t used;
    uint64_t total;
};

void pass_sha256_init(struct pass_sha256 *s);
void pass_sha256_update(struct pass_sha256 *s, const void *data, uint32_t length);
void pass_sha256_final(struct pass_sha256 *s, uint8_t out[32]);

/* Makes a record for password with the given salt. Returns -1 if out is too small. */
int pass_make_record(const char *password, const uint8_t salt[PASS_SALT_BYTES], char *out, uint32_t size);
/* 1 if password matches the record, 0 otherwise (always 0 for "!" or garbage). */
int pass_check(const char *password, const char *record);

#endif
