#include "../common/local_identity.h"
namespace cp {
struct LocalAuth::Impl {
    HMODULE module;
    void *key;
    Bytes cert;
#define API(ret, name, args)                                                                       \
    typedef ret(__cdecl *name##Type) args;                                                         \
    name##Type name
    API(void *, d2i_CMS_ContentInfo, (void **, const U8 **, long));
    API(void, CMS_ContentInfo_free, (void *));
    API(void *, CMS_get1_certs, (void *));
    API(int, OPENSSL_sk_num, (const void *));
    API(void *, OPENSSL_sk_value, (const void *, int));
    API(void, OPENSSL_sk_free, (void *));
    API(void *, X509_get_pubkey, (void *));
    API(void, X509_free, (void *));
    API(void *, d2i_PKCS8_PRIV_KEY_INFO, (void **, const U8 **, long));
    API(void, PKCS8_PRIV_KEY_INFO_free, (void *));
    API(void *, EVP_PKCS82PKEY, (const void *));
    API(void, EVP_PKEY_free, (void *));
    API(int, EVP_PKEY_eq, (const void *, const void *));
    API(int, EVP_PKEY_get_group_name, (const void *, char *, size_t, size_t *));
    API(void *, EVP_PKEY_CTX_new, (void *, void *));
    API(void, EVP_PKEY_CTX_free, (void *));
    API(int, EVP_PKEY_sign_init, (void *));
    API(int, EVP_PKEY_verify_init, (void *));
    API(int, EVP_PKEY_CTX_set_signature_md, (void *, const void *));
    API(const void *, EVP_sha256, ());
    API(int, EVP_PKEY_sign, (void *, U8 *, size_t *, const U8 *, size_t));
    API(int, EVP_PKEY_verify, (void *, const U8 *, size_t, const U8 *, size_t));
#undef API
    FARPROC symbol(const char *n) {
        FARPROC p = GetProcAddress(module, n);
        require(p != 0, "OpenSSL accessory authentication API missing");
        return p;
    }
    explicit Impl(const std::wstring &dll) : module(0), key(0) {
        require(dll.size() > 3 && dll[1] == ':' && (dll[2] == '/' || dll[2] == '\\'),
                "Authentication crypto DLL requires absolute path");
        module = LoadLibraryExW(dll.c_str(), 0, LOAD_WITH_ALTERED_SEARCH_PATH);
        require(module != 0, "Cannot load authentication crypto library");
        try {
#define LOAD(n) n = (n##Type)symbol(#n)
            LOAD(d2i_CMS_ContentInfo);
            LOAD(CMS_ContentInfo_free);
            LOAD(CMS_get1_certs);
            LOAD(OPENSSL_sk_num);
            LOAD(OPENSSL_sk_value);
            LOAD(OPENSSL_sk_free);
            LOAD(X509_get_pubkey);
            LOAD(X509_free);
            LOAD(d2i_PKCS8_PRIV_KEY_INFO);
            LOAD(PKCS8_PRIV_KEY_INFO_free);
            LOAD(EVP_PKCS82PKEY);
            LOAD(EVP_PKEY_free);
            LOAD(EVP_PKEY_eq);
            LOAD(EVP_PKEY_get_group_name);
            LOAD(EVP_PKEY_CTX_new);
            LOAD(EVP_PKEY_CTX_free);
            LOAD(EVP_PKEY_sign_init);
            LOAD(EVP_PKEY_verify_init);
            LOAD(EVP_PKEY_CTX_set_signature_md);
            LOAD(EVP_sha256);
            LOAD(EVP_PKEY_sign);
            LOAD(EVP_PKEY_verify);
#undef LOAD
        } catch (...) {
            FreeLibrary(module);
            throw;
        }
    }
    ~Impl() {
        if (key)
            EVP_PKEY_free(key);
        FreeLibrary(module);
    }
    void load(const std::wstring &directory) {
        cert = readFile(directory + L"/certificate.p7b", 65525);
        Bytes secret = readFile(directory + L"/identity.pk8", 4096);
        require(!secret.empty() && !cert.empty(), "Empty accessory identity");
        const U8 *p = &secret[0];
        void *info = d2i_PKCS8_PRIV_KEY_INFO(0, &p, long(secret.size()));
        bool complete = p == &secret[0] + secret.size();
        SecureZeroMemory(&secret[0], secret.size());
        if (info) {
            key = EVP_PKCS82PKEY(info);
            PKCS8_PRIV_KEY_INFO_free(info);
        }
        require(key != 0 && complete, "Invalid PKCS8 accessory private key");
        char group[80] = {0};
        size_t length = 0;
        require(EVP_PKEY_get_group_name(key, group, sizeof(group), &length) == 1 &&
                    std::string(group) == "prime256v1",
                "Accessory identity must use P256");
        p = &cert[0];
        void *cms = d2i_CMS_ContentInfo(0, &p, long(cert.size()));
        require(cms != 0, "Invalid accessory certificate container");
        void *certificates = CMS_get1_certs(cms);
        CMS_ContentInfo_free(cms);
        int count = certificates ? OPENSSL_sk_num(certificates) : 0;
        bool matches = false;
        for (int i = 0; i < count; ++i) {
            void *c = OPENSSL_sk_value(certificates, i);
            void *publicKey = X509_get_pubkey(c);
            if (publicKey) {
                matches = EVP_PKEY_eq(key, publicKey) == 1;
                EVP_PKEY_free(publicKey);
            }
            X509_free(c);
        }
        if (certificates)
            OPENSSL_sk_free(certificates);
        require(count == 1 && matches && p == &cert[0] + cert.size(),
                "Accessory certificate/private key mismatch or invalid container");
    }
};
LocalAuth::LocalAuth(const std::wstring &directory, const std::wstring &dll) : impl_(0) {
    impl_ = new Impl(dll);
    try {
        impl_->load(directory);
    } catch (...) {
        delete impl_;
        impl_ = 0;
        throw;
    }
}
LocalAuth::~LocalAuth() {
    delete impl_;
}
Bytes LocalAuth::certificate() {
    return impl_->cert;
}
Bytes LocalAuth::signChallenge(const Bytes &digest) {
    require(digest.size() == 32, "MFi v3 challenge must be 32 bytes");
    void *ctx = impl_->EVP_PKEY_CTX_new(impl_->key, 0);
    require(ctx != 0, "Cannot allocate accessory signer");
    Bytes der(80), raw(64, 0);
    size_t length = der.size();
    try {
        require(impl_->EVP_PKEY_sign_init(ctx) == 1 &&
                    impl_->EVP_PKEY_CTX_set_signature_md(ctx, impl_->EVP_sha256()) == 1 &&
                    impl_->EVP_PKEY_sign(ctx, &der[0], &length, &digest[0], digest.size()) == 1,
                "Accessory signature failed");
        // EVP_PKEY_sign consumes the prehashed challenge; it must not hash it again.
        require(impl_->EVP_PKEY_verify_init(ctx) == 1 &&
                    impl_->EVP_PKEY_CTX_set_signature_md(ctx, impl_->EVP_sha256()) == 1 &&
                    impl_->EVP_PKEY_verify(ctx, &der[0], length, &digest[0], digest.size()) == 1,
                "Accessory signature self-verification failed");
        require(length >= 8 && length <= 72 && der[0] == 0x30 && der[1] == length - 2,
                "Invalid ECDSA sequence");
        size_t at = 2;
        for (unsigned component = 0; component < 2; ++component) {
            require(at + 2 <= length && der[at++] == 2, "Invalid ECDSA integer");
            size_t n = der[at++];
            require(n >= 1 && n <= 33 && at + n <= length && !(der[at] & 0x80),
                    "Invalid ECDSA integer size/sign");
            if (n == 33) {
                require(der[at] == 0, "Oversized ECDSA scalar");
                ++at;
                --n;
            }
            std::copy(der.begin() + at, der.begin() + at + n,
                      raw.begin() + component * 32 + 32 - n);
            at += n;
        }
        require(at == length, "Trailing ECDSA data");
        impl_->EVP_PKEY_CTX_free(ctx);
        return raw;
    } catch (...) {
        impl_->EVP_PKEY_CTX_free(ctx);
        throw;
    }
}
} // namespace cp
