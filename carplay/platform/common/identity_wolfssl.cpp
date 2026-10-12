#include "local_identity.h"
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/asn.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <algorithm>
#include <sstream>
namespace cp {
static bool exactDer(const Bytes &b) {
    if (b.size() < 2 || b[0] != 0x30)
        return false;
    size_t offset = 2, length = b[1];
    if (length & 128) {
        size_t n = length & 127;
        if (!n || n > 4 || b.size() < 2 + n || b[2] == 0)
            return false;
        length = 0;
        for (size_t i = 0; i < n; ++i)
            length = (length << 8) | b[offset++];
        if (length < 128)
            return false;
    }
    return length == b.size() - offset;
}
struct LocalAuth::Impl {
    ecc_key key;
    WC_RNG rng;
    Bytes cert;
    Impl() {
        require(wc_ecc_init(&key) == 0, "ECDSA initialization");
        if (wc_InitRng(&rng) != 0) {
            wc_ecc_free(&key);
            require(false, "Signer RNG initialization");
        }
    }
    ~Impl() {
        wc_ecc_free(&key);
        wc_FreeRng(&rng);
    }
    void load(const std::wstring &dir) {
        cert = readFile(dir + L"/certificate.p7b", 65525);
        Bytes leaf = readFile(dir + L"/certificate.der", 65525),
              secret = readFile(dir + L"/identity.pk8", 8192);
        require(exactDer(cert) && exactDer(leaf) && exactDer(secret),
                "Trailing or malformed identity DER");
        require(!leaf.empty() &&
                    std::search(cert.begin(), cert.end(), leaf.begin(), leaf.end()) != cert.end(),
                "Identity certificate not in PKCS7 container");
        word32 offset = 0;
        int rc = wc_EccPrivateKeyDecode(&secret[0], &offset, &key, secret.size());
        SecureZeroMemory(&secret[0], secret.size());
        require(rc == 0 && wc_ecc_size(&key) == 32, "Invalid P256 accessory identity");
        // Verify a challenge against the leaf certificate, not just against the private key.
        ecc_key publicKey;
        wc_ecc_init(&publicKey);
        DecodedCert decoded;
        wc_InitDecodedCert(&decoded, &leaf[0], leaf.size(), 0);
        rc = wc_ParseCert(&decoded, CERT_TYPE, NO_VERIFY, 0);
        // This is local key-possession checking, not certificate-chain trust.
        // MFi leaves carry Apple's critical extension (1.2.840.113635.100.6.36),
        // whose authorization semantics are checked by the iPhone. wolfSSL has
        // parsed the complete certificate but cannot interpret that extension.
        // Keep normal TLS/remote certificate verification entirely unchanged.
        if(rc==ASN_CRIT_EXT_E)rc=0;
        const char *stage="certificate parse";
        offset = 0;
        if (!rc){
            stage="certificate public key";
            rc = wc_EccPublicKeyDecode(decoded.publicKey, &offset, &publicKey, decoded.pubKeySize);
        }
        U8 digest[32] = {0}, signature[80];
        word32 length = sizeof(signature);
        int valid = 0;
        if (!rc){
            stage="private key signing";
            rc = wc_ecc_sign_hash(digest, 32, signature, &length, &rng, &key);
        }
        if (!rc){
            stage="certificate signature verification";
            rc = wc_ecc_verify_hash(signature, length, digest, 32, &valid, &publicKey);
        }
        wc_FreeDecodedCert(&decoded);
        wc_ecc_free(&publicKey);
        if(rc){std::ostringstream error;error<<"Accessory "<<stage<<" failed (wolfSSL "<<rc<<")";
            throw std::runtime_error(error.str());}
        require(valid == 1, "Accessory certificate/private key mismatch");
    }
};
LocalAuth::LocalAuth(const std::wstring &dir, const std::wstring &) : impl_(new Impl) {
    try {
        impl_->load(dir);
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
    require(digest.size() == 32, "MFi challenge length");
    Bytes der(80), raw(64);
    word32 size = der.size();
    require(wc_ecc_sign_hash(&digest[0], 32, &der[0], &size, &impl_->rng, &impl_->key) == 0,
            "MFi signature failed");
    word32 r = 32, s = 32;
    require(wc_ecc_sig_to_rs(&der[0], size, &raw[0], &r, &raw[32], &s) == 0,
            "MFi signature encoding");
    // wolfCrypt returns minimal big-endian integers: pad each scalar to 32 bytes.
    require(r <= 32 && s <= 32, "ECDSA scalar length");
    Bytes result(64, 0);
    std::copy(raw.begin(), raw.begin() + r, result.begin() + 32 - r);
    std::copy(raw.begin() + 32, raw.begin() + 32 + s, result.begin() + 64 - s);
    return result;
}
} // namespace cp
