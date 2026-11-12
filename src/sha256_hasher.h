#pragma once

// ===== DAY 29 ADDED BEGIN 使用OpenSSL EVP增量计算SHA256 =====
#include <openssl/evp.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

class Sha256Hasher
{
public:
    Sha256Hasher() noexcept = default;

    Sha256Hasher(const Sha256Hasher&) = delete;
    Sha256Hasher& operator=(const Sha256Hasher&) = delete;

    ~Sha256Hasher() noexcept
    {
        EVP_MD_CTX_free(context_);
    }

    bool update(std::string_view data) noexcept
    {
        if (failed_ || finalized_) return false;
        if (data.empty()) return true;
        if (!initialize()) return false;
        if (EVP_DigestUpdate(context_, data.data(), data.size()) != 1)
        {
            fail();
            return false;
        }
        return true;
    }

    bool finish(std::string& output) noexcept
    {
        if (failed_ || finalized_ || !initialize()) return false;
        std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
        unsigned int size = 0;
        if (EVP_DigestFinal_ex(context_, bytes.data(), &size) != 1 || size != 32)
        {
            fail();
            return false;
        }

        static constexpr char digits[] = "0123456789abcdef";
        std::string hex;
        try
        {
            hex.resize(size * 2);
        }
        catch (...)
        {
            fail();
            return false;
        }
        for (std::size_t index = 0; index < size; ++index)
        {
            hex[index * 2] = digits[bytes[index] >> 4];
            hex[index * 2 + 1] = digits[bytes[index] & 0x0f];
        }

        EVP_MD_CTX_free(context_);
        context_ = nullptr;
        finalized_ = true;
        output = std::move(hex);
        return true;
    }

    bool good() const noexcept
    {
        return !failed_;
    }

private:
    bool initialize() noexcept
    {
        if (context_ != nullptr) return true;
        context_ = EVP_MD_CTX_new();
        if (context_ != nullptr &&
            EVP_DigestInit_ex(context_, EVP_sha256(), nullptr) == 1)
            return true;
        fail();
        return false;
    }

    void fail() noexcept
    {
        EVP_MD_CTX_free(context_);
        context_ = nullptr;
        failed_ = true;
    }

    EVP_MD_CTX* context_ = nullptr;
    bool finalized_ = false;
    bool failed_ = false;
};
// ===== DAY 29 ADDED END =====
