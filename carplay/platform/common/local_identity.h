#ifndef MRN_LOCAL_AUTH_H
#define MRN_LOCAL_AUTH_H
#include "native_services.h"
namespace cp {
class LocalAuth : public AuthProvider {
    struct Impl;
    Impl *impl_;
    LocalAuth(const LocalAuth &);
    LocalAuth &operator=(const LocalAuth &);

  public:
    LocalAuth(const std::wstring &directory, const std::wstring &cryptoDll);
    ~LocalAuth();
    Bytes certificate();
    Bytes signChallenge(const Bytes &digest);
};
} // namespace cp
#endif
