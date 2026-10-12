#ifndef MRN_PAIRING_H
#define MRN_PAIRING_H
#include "../interfaces/crypto_provider.h"
namespace cp {
class Pairing {
    Pairing(const Pairing &);
    Pairing &operator=(const Pairing &);
    CryptoProvider &c_;
    Bytes seed_, salt_, exponent_, verifier_, serverPublic_, setupKey_, ephemeral_, peerEphemeral_,
        verifyKey_;
    Value stored_;
    std::string identifier_, serial_, displayUuid_;
    PairingStore &store_;
    unsigned setupState_, verifyState_;
    Bytes setup(const std::map<U8, Bytes> &tlv);
    Bytes verify(const std::map<U8, Bytes> &tlv);

  public:
    Bytes sharedSecret, readKey, writeKey;
    Pairing(CryptoProvider &c, PairingStore &store);
    Bytes publicKey();
    std::string identifier() const {
        return identifier_;
    }
    std::string serialNumber() const { return serial_; }
    std::string displayUuid() const { return displayUuid_; }
    Bytes handle(bool verification, const Bytes &body);
    Bytes mfiSap(AuthProvider &auth, const Bytes &body);
};
} // namespace cp
#endif
