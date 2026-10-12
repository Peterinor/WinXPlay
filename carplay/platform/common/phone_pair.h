#ifndef MRN_PHONE_PAIR_H
#define MRN_PHONE_PAIR_H
#include "native_services.h"
namespace cp {
// Lockdown host trust is independent of the accessory MFi identity.
Value generatePhoneIdentity(const Bytes &devicePublicKey);
void validatePhoneIdentity(const Value &pair,const Bytes &devicePublicKey);
class PhonePairChannel {
  public:
    virtual ~PhonePairChannel() {}
    virtual Value exchange(const Value &request) = 0;
};
std::wstring phonePairPath(const std::wstring &folder, const std::string &udid);
Value ensurePhonePair(PhonePairChannel &channel, const std::wstring &folder,
                     const std::string &udid);
void invalidatePhonePair(const std::wstring &folder, const std::string &udid);
}
#endif
