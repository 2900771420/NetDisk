// ===== DAY 33A ADDED BEGIN 隐藏OpenSSL EVP并实现SHA256接口 =====
#include <netdisk/crypto/sha256.h>

#include <openssl/evp.h>

#include <array>
#include <cstddef>
#include <utility>

namespace
{

EVP_MD_CTX* as_context(void* context) noexcept
{
    return static_cast<EVP_MD_CTX*>(context);
}

}

bool is_canonical_sha256(std::string_view value) noexcept
{
    if (value.size() != 64) return false;
    for (const char ch : value)
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    return true;
}

Sha256Hasher::~Sha256Hasher() noexcept
{
    EVP_MD_CTX_free(as_context(context_));
}

bool Sha256Hasher::update(std::string_view data) noexcept
{
    if (failed_ || finalized_) return false;
    if (data.empty()) return true;
    if (!initialize()) return false;
    if (EVP_DigestUpdate(as_context(context_), data.data(), data.size()) != 1)
    {
        fail();
        return false;
    }
    return true;
}

bool Sha256Hasher::finish(std::string& output) noexcept
{
    if (failed_ || finalized_ || !initialize()) return false;
    std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
    unsigned int size = 0;
    if (EVP_DigestFinal_ex(as_context(context_), bytes.data(), &size) != 1 ||
        size != 32)
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

    EVP_MD_CTX_free(as_context(context_));
    context_ = nullptr;
    finalized_ = true;
    output = std::move(hex);
    return true;
}

bool Sha256Hasher::good() const noexcept
{
    return !failed_;
}

bool Sha256Hasher::initialize() noexcept
{
    if (context_ != nullptr) return true;
    EVP_MD_CTX* created = EVP_MD_CTX_new();
    context_ = created;
    if (created != nullptr && EVP_DigestInit_ex(created, EVP_sha256(), nullptr) == 1)
        return true;
    fail();
    return false;
}

void Sha256Hasher::fail() noexcept
{
    EVP_MD_CTX_free(as_context(context_));
    context_ = nullptr;
    failed_ = true;
}
// ===== DAY 33A ADDED END =====
