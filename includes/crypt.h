#ifndef XHTTP_CRYPT_H
#define XHTTP_CRYPT_H

#include <iostream>
#include <vector>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <cstring>
#include <stdexcept>

const std::string AES_CRYPT_KEY {"your_secret_key_please_replace_m"}; // Must be 32 bytes for AES-256
#define AES_IV_SIZE 12  // GCM standard IV size (96 bits)
#define TAG_SIZE 16     // Authentication tag size

class AESGCM {
public:
    static std::vector<uint8_t> encrypt(const std::vector<uint8_t> &plaintext) {
        if (AES_CRYPT_KEY.size() != 32) {
            throw std::runtime_error("AES key must be 32 bytes for AES-256-GCM.");
        }

        std::vector<uint8_t> ciphertext(AES_IV_SIZE + plaintext.size() + TAG_SIZE); // IV + Ciphertext + Tag
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("Failed to create EVP_CIPHER_CTX");

        uint8_t iv[AES_IV_SIZE];
        if (RAND_bytes(iv, AES_IV_SIZE) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to generate IV");
        }
        std::memcpy(ciphertext.data(), iv, AES_IV_SIZE); // Store IV at the beginning

        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, AES_IV_SIZE, nullptr) != 1 ||
            EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                               reinterpret_cast<const unsigned char *>(AES_CRYPT_KEY.data()), iv) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Encryption initialization failed");
        }

        int len;
        if (EVP_EncryptUpdate(ctx, ciphertext.data() + AES_IV_SIZE, &len, plaintext.data(), plaintext.size()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Encryption failed");
        }

        int final_len;
        if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + AES_IV_SIZE + len, &final_len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Encryption finalization failed");
        }

        uint8_t tag[TAG_SIZE];
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to get encryption tag");
        }

        std::memcpy(ciphertext.data() + AES_IV_SIZE + len, tag, TAG_SIZE); // Append tag at the end
        EVP_CIPHER_CTX_free(ctx);

        return ciphertext;
    }

    static std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext) {
        if (AES_CRYPT_KEY.size() != 32) {
            throw std::runtime_error("AES key must be 32 bytes for AES-256-GCM.");
        }

        if (ciphertext.size() < AES_IV_SIZE + TAG_SIZE) {
            throw std::runtime_error("Ciphertext too short");
        }

        std::vector<uint8_t> plaintext(ciphertext.size() - AES_IV_SIZE - TAG_SIZE);

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("Failed to create EVP_CIPHER_CTX");

        uint8_t iv[AES_IV_SIZE];
        std::memcpy(iv, ciphertext.data(), AES_IV_SIZE); // Extract IV
        uint8_t tag[TAG_SIZE];
        std::memcpy(tag, ciphertext.data() + ciphertext.size() - TAG_SIZE, TAG_SIZE); // Extract Tag

        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, AES_IV_SIZE, nullptr) != 1 ||
            EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                               reinterpret_cast<const unsigned char *>(AES_CRYPT_KEY.data()), iv) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Decryption initialization failed");
        }

        int len;
        if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data() + AES_IV_SIZE, plaintext.size()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Decryption failed");
        }

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, tag) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to set decryption tag");
        }

        int final_len;
        if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &final_len) <= 0) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Authentication failed");
        }

        EVP_CIPHER_CTX_free(ctx);
        plaintext.resize(len + final_len); // Trim the buffer to actual plaintext size
        return plaintext;
    }
};

#endif // XHTTP_CRYPT_H
