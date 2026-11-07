#pragma once

// The ReXGlue SDK's aes_128 interface (thirdparty/aes_128/aes.h), which skylanders-portal's
// figure_crypto.cpp uses off Windows, here on OpenSSL (aes_md5_openssl.cpp) so the launcher needs
// no SDK tree. The "round keys" buffer only holds the 16-byte key: OpenSSL expands it itself.

#include <stddef.h>
#include <stdint.h>

#define AES_ROUND_KEY_SIZE 176

#ifdef __cplusplus
extern "C" {
#endif

void aes_key_schedule_128(const uint8_t* key, uint8_t* round_keys);
void aes_encrypt_128(const uint8_t* round_keys, const uint8_t* plaintext, uint8_t* ciphertext);
void aes_decrypt_128(const uint8_t* round_keys, const uint8_t* ciphertext, uint8_t* plaintext);

#ifdef __cplusplus
}
#endif
