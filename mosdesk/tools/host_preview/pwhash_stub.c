/* Stand-in for mosdesk_password_hash (the real one needs the SDK's minimaSSL
 * submodule, which isn't in the zip). Deterministic, salted, self-consistent. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
bool mosdesk_password_hash(const uint8_t salt[16], const char* password, uint8_t digest[32]) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 16; i++) { h ^= salt[i]; h *= 1099511628211ull; }
    for (const char* p = password; *p; p++) { h ^= (uint8_t)*p; h *= 1099511628211ull; }
    for (int i = 0; i < 32; i++) { h ^= (uint64_t)i; h *= 1099511628211ull; digest[i] = (uint8_t)(h >> 24); }
    return true;
}
