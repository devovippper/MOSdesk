#ifndef MOSDESK_PASSWORD_HASH_H
#define MOSDESK_PASSWORD_HASH_H

#include <stdbool.h>
#include <stdint.h>

#ifndef MOSDESK_PASSWORD_SALT_SIZE
#define MOSDESK_PASSWORD_SALT_SIZE 16u
#endif
#ifndef MOSDESK_PASSWORD_HASH_SIZE
#define MOSDESK_PASSWORD_HASH_SIZE 32u
#endif

bool mosdesk_password_hash(
    const uint8_t salt[MOSDESK_PASSWORD_SALT_SIZE],
    const char* password,
    uint8_t digest[MOSDESK_PASSWORD_HASH_SIZE]);

#endif
