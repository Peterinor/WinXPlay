#ifndef CP_SESSION_RUNTIME_H
#define CP_SESSION_RUNTIME_H
#include "../core/protocol.h"
namespace cp {
// One worker, cancellation flag and recursive lock per receiver. This is not
// a general scheduler: it encapsulates the existing receiver's OS operations.
class SessionRuntime {
  public:
    virtual ~SessionRuntime() {}
    virtual void start(void (*entry)(void *), void *context) = 0;
    virtual void cancel() = 0;
    virtual bool cancelled() const = 0;
    virtual void join() = 0;
    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual U32 milliseconds() const = 0;
    virtual U64 ntpTime() const = 0;
    virtual void saveReport(const Value &) = 0;
};
class SessionLock {
    SessionRuntime &runtime_;
    SessionLock(const SessionLock &);
    SessionLock &operator=(const SessionLock &);

  public:
    explicit SessionLock(SessionRuntime &r) : runtime_(r) {
        runtime_.lock();
    }
    ~SessionLock() {
        runtime_.unlock();
    }
};
} // namespace cp
#endif
