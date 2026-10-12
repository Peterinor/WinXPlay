#ifndef CP_RECEIVER_CONTEXT_H
#define CP_RECEIVER_CONTEXT_H
#include "../../core/receiver.h"
#include "native_services.h"
namespace cp {
// Concrete application composition shared by Windows and CE. No such factory
// is called from core. UI callbacks stay here, outside the compressed pipeline.
class ReceiverContext {
    struct Impl;
    Impl *p_;
    ReceiverContext(const ReceiverContext &);
    ReceiverContext &operator=(const ReceiverContext &);

  public:
    ReceiverContext(VideoCallback, void *, HWND videoWindow = 0);
    ~ReceiverContext();
    ReceiverServices &services();
};
MediaCapabilities applicationMediaCapabilities();
std::string applicationAccessorySerialNumber();
} // namespace cp
#endif
