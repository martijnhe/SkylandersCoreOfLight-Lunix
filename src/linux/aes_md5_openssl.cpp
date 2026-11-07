// AES-128-ECB of one block and MD5 for skylanders-portal's figure_crypto.cpp off Windows (aes.h).

#include "aes.h"

#include <openssl/evp.h>

#include <cstring>

namespace {

void Aes(const uint8_t* key, const uint8_t* in, uint8_t* out, bool encrypt) {
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  int n = 0;
  if (ctx && EVP_CipherInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key, nullptr, encrypt ? 1 : 0) == 1) {
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    if (EVP_CipherUpdate(ctx, out, &n, in, 16) != 1 || n != 16) std::memset(out, 0, 16);
  } else {
    std::memset(out, 0, 16);
  }
  EVP_CIPHER_CTX_free(ctx);
}

}  // namespace

extern "C" void aes_key_schedule_128(const uint8_t* key, uint8_t* round_keys) {
  std::memset(round_keys, 0, AES_ROUND_KEY_SIZE);
  std::memcpy(round_keys, key, 16);
}

extern "C" void aes_encrypt_128(const uint8_t* round_keys, const uint8_t* plaintext, uint8_t* ciphertext) {
  Aes(round_keys, plaintext, ciphertext, true);
}

extern "C" void aes_decrypt_128(const uint8_t* round_keys, const uint8_t* ciphertext, uint8_t* plaintext) {
  Aes(round_keys, ciphertext, plaintext, false);
}

// libmspack's md5.c: the digest of `len` bytes into `resblock` (16 bytes).
extern "C" void* md5_buffer(const char* buffer, size_t len, void* resblock) {
  unsigned int n = 0;
  EVP_Digest(buffer, len, static_cast<unsigned char*>(resblock), &n, EVP_md5(), nullptr);
  return resblock;
}
