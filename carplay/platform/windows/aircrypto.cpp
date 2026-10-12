#include "../common/native_services.h"
#include "../common/native_crypto.h"
#include <wincrypt.h>
namespace cp {
struct AirCrypto::Impl {
    HMODULE module;
#define API(ret, n, args)                                                                          \
    typedef ret(__cdecl *n##Fn) args;                                                              \
    n##Fn n
    API(int, RAND_bytes, (U8 *, int));
    API(const void *, EVP_get_digestbyname, (const char *));
    API(int, EVP_Digest, (const void *, size_t, U8 *, unsigned *, const void *, void *));
    API(U8 *, HMAC, (const void *, const void *, int, const U8 *, size_t, U8 *, unsigned *));
    API(void *, EVP_PKEY_new_raw_private_key_ex,
        (void *, const char *, const char *, const U8 *, size_t));
    API(void *, EVP_PKEY_new_raw_public_key_ex,
        (void *, const char *, const char *, const U8 *, size_t));
    API(int, EVP_PKEY_get_raw_public_key, (const void *, U8 *, size_t *));
    API(void, EVP_PKEY_free, (void *));
    API(void *, EVP_PKEY_CTX_new, (void *, void *));
    API(void, EVP_PKEY_CTX_free, (void *));
    API(int, EVP_PKEY_derive_init, (void *));
    API(int, EVP_PKEY_derive_set_peer, (void *, void *));
    API(int, EVP_PKEY_derive, (void *, U8 *, size_t *));
    API(void *, EVP_MD_CTX_new, ());
    API(void, EVP_MD_CTX_free, (void *));
    API(int, EVP_DigestSignInit, (void *, void **, const void *, void *, void *));
    API(int, EVP_DigestSign, (void *, U8 *, size_t *, const U8 *, size_t));
    API(int, EVP_DigestVerifyInit, (void *, void **, const void *, void *, void *));
    API(int, EVP_DigestVerify, (void *, const U8 *, size_t, const U8 *, size_t));
    API(void *, EVP_CIPHER_CTX_new, ());
    API(void, EVP_CIPHER_CTX_free, (void *));
    API(const void *, EVP_aes_128_ctr, ());
    API(int, EVP_EncryptInit_ex, (void *, const void *, void *, const U8 *, const U8 *));
    API(int, EVP_EncryptUpdate, (void *, U8 *, int *, const U8 *, int));
    API(int, EVP_EncryptFinal_ex, (void *, U8 *, int *));
    API(void *, BN_bin2bn, (const U8 *, int, void *));
    API(void *, BN_new, ());
    API(void, BN_clear_free, (void *));
    API(void *, BN_CTX_new, ());
    API(void, BN_CTX_free, (void *));
    API(void, BN_set_flags, (void *, int));
    API(int, BN_bn2binpad, (const void *, U8 *, int));
    API(int, BN_mod_exp, (void *, const void *, const void *, const void *, void *));
    API(int, BN_mod_mul, (void *, const void *, const void *, const void *, void *));
    API(int, BN_mod_add, (void *, const void *, const void *, const void *, void *));
    API(int, BN_nnmod, (void *, const void *, const void *, void *));
#undef API
    explicit Impl(const std::wstring &dll) {
        module = LoadLibraryExW(dll.c_str(), 0, LOAD_WITH_ALTERED_SEARCH_PATH);
        require(module != 0, "AirPlay crypto DLL unavailable");
        try {
#define LOAD(n)                                                                                    \
    n = (n##Fn)GetProcAddress(module, #n);                                                         \
    require(n != 0, "AirPlay crypto symbol missing")
            LOAD(RAND_bytes);
            LOAD(EVP_get_digestbyname);
            LOAD(EVP_Digest);
            LOAD(HMAC);
            LOAD(EVP_PKEY_new_raw_private_key_ex);
            LOAD(EVP_PKEY_new_raw_public_key_ex);
            LOAD(EVP_PKEY_get_raw_public_key);
            LOAD(EVP_PKEY_free);
            LOAD(EVP_PKEY_CTX_new);
            LOAD(EVP_PKEY_CTX_free);
            LOAD(EVP_PKEY_derive_init);
            LOAD(EVP_PKEY_derive_set_peer);
            LOAD(EVP_PKEY_derive);
            LOAD(EVP_MD_CTX_new);
            LOAD(EVP_MD_CTX_free);
            LOAD(EVP_DigestSignInit);
            LOAD(EVP_DigestSign);
            LOAD(EVP_DigestVerifyInit);
            LOAD(EVP_DigestVerify);
            LOAD(EVP_CIPHER_CTX_new);
            LOAD(EVP_CIPHER_CTX_free);
            LOAD(EVP_aes_128_ctr);
            LOAD(EVP_EncryptInit_ex);
            LOAD(EVP_EncryptUpdate);
            LOAD(EVP_EncryptFinal_ex);
            LOAD(BN_bin2bn);
            LOAD(BN_new);
            LOAD(BN_clear_free);
            LOAD(BN_CTX_new);
            LOAD(BN_CTX_free);
            LOAD(BN_set_flags);
            LOAD(BN_bn2binpad);
            LOAD(BN_mod_exp);
            LOAD(BN_mod_mul);
            LOAD(BN_mod_add);
            LOAD(BN_nnmod);
#undef LOAD
        } catch (...) {
            FreeLibrary(module);
            throw;
        }
    }
    ~Impl() {
        FreeLibrary(module);
    }
};
AirCrypto::AirCrypto(const std::wstring &dll) : cipher_(dll), p_(new Impl(dll)) {}
AirCrypto::~AirCrypto() {
    delete p_;
}
Bytes AirCrypto::random(unsigned n) {
    require(n > 0 && n <= 4096, "Random size limit");
    Bytes b(n);
    require(p_->RAND_bytes(&b[0], n) == 1, "Random generator failed");
    return b;
}
Bytes AirCrypto::hash(const char *name, const Bytes &data) {
    Bytes b(64);
    unsigned n = 64;
    const void *md = p_->EVP_get_digestbyname(name);
    require(md && p_->EVP_Digest(data.empty() ? static_cast<const void *>("")
                                              : static_cast<const void *>(&data[0]),
                                 data.size(), &b[0], &n, md, 0) == 1,
            "Digest failed");
    b.resize(n);
    return b;
}
Bytes AirCrypto::hkdf(const Bytes &key, const std::string &salt, const std::string &info) {
    Bytes prk(64), out(64), input = bytes(info);
    input.push_back(1);
    unsigned n = 64;
    const void *md = p_->EVP_get_digestbyname("SHA512");
    require(p_->HMAC(md, salt.data(), int(salt.size()), &key[0], key.size(), &prk[0], &n) != 0,
            "HKDF extract failed");
    require(p_->HMAC(md, &prk[0], int(prk.size()), &input[0], input.size(), &out[0], &n) != 0,
            "HKDF expand failed");
    SecureZeroMemory(&prk[0], prk.size());
    out.resize(32);
    return out;
}
Bytes AirCrypto::publicKey(const char *type, const Bytes &seed) {
    require(seed.size() == 32, "Raw key size");
    void *key = p_->EVP_PKEY_new_raw_private_key_ex(0, type, 0, &seed[0], 32);
    require(key != 0, "Cannot load raw private key");
    Bytes out(32);
    size_t n = 32;
    int ok = p_->EVP_PKEY_get_raw_public_key(key, &out[0], &n);
    p_->EVP_PKEY_free(key);
    require(ok == 1 && n == 32, "Public key failed");
    return out;
}
Bytes AirCrypto::shared(const Bytes &seed, const Bytes &peer) {
    require(seed.size() == 32 && peer.size() == 32, "X25519 key size");
    void *a = p_->EVP_PKEY_new_raw_private_key_ex(0, "X25519", 0, &seed[0], 32);
    void *b = p_->EVP_PKEY_new_raw_public_key_ex(0, "X25519", 0, &peer[0], 32);
    void *ctx = a ? p_->EVP_PKEY_CTX_new(a, 0) : 0;
    Bytes out(32);
    size_t n = 32;
    bool ok = ctx && b && p_->EVP_PKEY_derive_init(ctx) == 1 &&
              p_->EVP_PKEY_derive_set_peer(ctx, b) == 1 &&
              p_->EVP_PKEY_derive(ctx, &out[0], &n) == 1;
    if (ctx)
        p_->EVP_PKEY_CTX_free(ctx);
    if (a)
        p_->EVP_PKEY_free(a);
    if (b)
        p_->EVP_PKEY_free(b);
    require(ok && n == 32 && out != Bytes(32, 0), "X25519 agreement rejected");
    return out;
}
Bytes AirCrypto::sign(const Bytes &seed, const Bytes &message) {
    require(seed.size() == 32 && !message.empty(), "Ed25519 input");
    void *key = p_->EVP_PKEY_new_raw_private_key_ex(0, "ED25519", 0, &seed[0], 32);
    void *ctx = p_->EVP_MD_CTX_new();
    Bytes out(64);
    size_t n = 64;
    bool ok = key && ctx && p_->EVP_DigestSignInit(ctx, 0, 0, 0, key) == 1 &&
              p_->EVP_DigestSign(ctx, &out[0], &n, &message[0], message.size()) == 1;
    if (ctx)
        p_->EVP_MD_CTX_free(ctx);
    if (key)
        p_->EVP_PKEY_free(key);
    require(ok && n == 64, "Ed25519 signing failed");
    return out;
}
bool AirCrypto::verify(const Bytes &raw, const Bytes &message, const Bytes &signature) {
    if (raw.size() != 32 || signature.size() != 64 || message.empty())
        return false;
    void *key = p_->EVP_PKEY_new_raw_public_key_ex(0, "ED25519", 0, &raw[0], 32);
    void *ctx = p_->EVP_MD_CTX_new();
    bool ok = key && ctx && p_->EVP_DigestVerifyInit(ctx, 0, 0, 0, key) == 1 &&
              p_->EVP_DigestVerify(ctx, &signature[0], 64, &message[0], message.size()) == 1;
    if (ctx)
        p_->EVP_MD_CTX_free(ctx);
    if (key)
        p_->EVP_PKEY_free(key);
    return ok;
}
Bytes AirCrypto::aesCtr(const Bytes &key, const Bytes &iv, const Bytes &data) {
    require(key.size() == 16 && iv.size() == 16 && data.size() <= 65536, "AES CTR size");
    void *ctx = p_->EVP_CIPHER_CTX_new();
    Bytes out(data.size() + 16);
    int n = 0, last = 0;
    bool ok = ctx && p_->EVP_EncryptInit_ex(ctx, p_->EVP_aes_128_ctr(), 0, &key[0], &iv[0]) == 1 &&
              p_->EVP_EncryptUpdate(ctx, &out[0], &n, &data[0], int(data.size())) == 1 &&
              p_->EVP_EncryptFinal_ex(ctx, &out[n], &last) == 1;
    if (ctx)
        p_->EVP_CIPHER_CTX_free(ctx);
    require(ok, "AES CTR failed");
    out.resize(n + last);
    return out;
}
Bytes AirCrypto::mod(const Bytes &a, const Bytes &b, const Bytes &n, char op) {
    require(!a.empty() && !b.empty() && !n.empty() && a.size() <= 384 && b.size() <= 384 &&
                n.size() == 384,
            "SRP integer size");
    void *aa = p_->BN_bin2bn(&a[0], int(a.size()), 0), *bb = p_->BN_bin2bn(&b[0], int(b.size()), 0),
         *nn = p_->BN_bin2bn(&n[0], int(n.size()), 0), *r = p_->BN_new(), *ctx = p_->BN_CTX_new();
    bool ok = aa && bb && nn && r && ctx;
    Bytes out(384);
    if (ok) {
        p_->BN_set_flags(bb, 4);
        int result = op == '^'   ? p_->BN_mod_exp(r, aa, bb, nn, ctx)
                     : op == '*' ? p_->BN_mod_mul(r, aa, bb, nn, ctx)
                     : op == '+' ? p_->BN_mod_add(r, aa, bb, nn, ctx)
                                 : p_->BN_nnmod(r, aa, nn, ctx);
        ok = result == 1 && p_->BN_bn2binpad(r, &out[0], 384) == 384;
    }
    if (aa)
        p_->BN_clear_free(aa);
    if (bb)
        p_->BN_clear_free(bb);
    if (nn)
        p_->BN_clear_free(nn);
    if (r)
        p_->BN_clear_free(r);
    if (ctx)
        p_->BN_CTX_free(ctx);
    require(ok, "SRP calculation failed");
    return out;
}
Bytes protectedRead(const std::wstring &path) {
    Bytes data = readFile(path, 1024 * 1024);
    require(!data.empty(), "Empty protected identity");
    DATA_BLOB in = {DWORD(data.size()), &data[0]}, out = {0, 0};
    require(CryptUnprotectData(&in, 0, 0, 0, 0, CRYPTPROTECT_UI_FORBIDDEN, &out) != FALSE,
            "Cannot decrypt saved receiver identity");
    Bytes plain(out.pbData, out.pbData + out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plain;
}
void protectedWrite(const std::wstring &path, const Bytes &data) {
    require(!data.empty(), "Empty identity");
    DATA_BLOB in = {DWORD(data.size()), const_cast<U8 *>(&data[0])}, out = {0, 0};
    require(CryptProtectData(&in, L"WinXPlay identity", 0, 0, 0, CRYPTPROTECT_UI_FORBIDDEN,
                             &out) != FALSE,
            "Cannot protect receiver identity");
    Bytes sealed(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    writeFile(path + L".tmp", sealed);
    require(MoveFileExW((path + L".tmp").c_str(), path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE,
            "Cannot save receiver identity");
}
} // namespace cp
