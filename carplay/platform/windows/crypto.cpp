#include "../common/native_cipher.h"
namespace cp {
FARPROC OpenSslCrypto::symbol(const char *n) {
    FARPROC p = GetProcAddress(module_, n);
    require(p != 0, "OpenSSL 3 API missing");
    return p;
}
OpenSslCrypto::OpenSslCrypto(const std::wstring &dll) : module_(0) {
    require(dll.size() > 3 && dll[1] == ':' && (dll[2] == '/' || dll[2] == '\\'),
            "Crypto DLL requires an absolute path");
    module_ = LoadLibraryExW(dll.c_str(), 0, LOAD_WITH_ALTERED_SEARCH_PATH);
    require(module_ != 0, "Cannot load x64 OpenSSL 3 library");
    try {
        create_ = (New)symbol("EVP_CIPHER_CTX_new");
        free_ = (Free)symbol("EVP_CIPHER_CTX_free");
        cipher_ = (Cipher)symbol("EVP_chacha20_poly1305");
        encInit_ = (Init)symbol("EVP_EncryptInit_ex");
        decInit_ = (Init)symbol("EVP_DecryptInit_ex");
        encUpdate_ = (Update)symbol("EVP_EncryptUpdate");
        decUpdate_ = (Update)symbol("EVP_DecryptUpdate");
        encFinal_ = (Final)symbol("EVP_EncryptFinal_ex");
        decFinal_ = (Final)symbol("EVP_DecryptFinal_ex");
        ctrl_ = (Ctrl)symbol("EVP_CIPHER_CTX_ctrl");
    } catch (...) {
        FreeLibrary(module_);
        throw;
    }
}
OpenSslCrypto::~OpenSslCrypto() {
    if (module_)
        FreeLibrary(module_);
}
Bytes OpenSslCrypto::run(bool enc, const Bytes &key, const Bytes &nonce, const Bytes &input,
                         const Bytes &aad) {
    require(key.size() == 32 && nonce.size() == 12 && input.size() <= 4 * 1024 * 1024 &&
                aad.size() <= 32768,
            "Invalid AEAD input");
    require(enc || input.size() >= 16, "Missing AEAD tag");
    void *ctx = create_();
    require(ctx != 0, "Cannot allocate cipher");
    Bytes out(input.size() + 32, 0);
    try {
        Init init = enc ? encInit_ : decInit_;
        Update update = enc ? encUpdate_ : decUpdate_;
        Final final = enc ? encFinal_ : decFinal_;
        require(init(ctx, cipher_(), 0, &key[0], &nonce[0]) == 1, "AEAD init failed");
        int n = 0, count = 0;
        require(aad.empty() || update(ctx, 0, &n, &aad[0], int(aad.size())) == 1,
                "AEAD AAD failed");
        size_t length = input.size() - (enc ? 0 : 16);
        require(update(ctx, &out[0], &n, input.empty() ? 0 : &input[0], int(length)) == 1,
                "AEAD update failed");
        count = n;
        Bytes tag;
        if (!enc) {
            tag = slice(input, length, 16);
            require(ctrl_(ctx, 0x11, 16, &tag[0]) == 1, "AEAD tag failed");
        }
        require(final(ctx, &out[count], &n) == 1, "Authentication tag rejected");
        count += n;
        if (enc) {
            require(ctrl_(ctx, 0x10, 16, &out[count]) == 1, "AEAD get tag failed");
            count += 16;
        }
        out.resize(count);
        free_(ctx);
        return out;
    } catch (...) {
        SecureZeroMemory(&out[0], out.size());
        free_(ctx);
        throw;
    }
}
} // namespace cp
