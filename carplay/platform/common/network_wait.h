#ifndef WINXPLAY_NETWORK_WAIT_H
#define WINXPLAY_NETWORK_WAIT_H
#include <string>
#include <stdexcept>
namespace cp {
// Network readiness is not a failed CarPlay authentication. Keep servicing iAP2
// while polling, and start the media deadline only after an address is ready.
class CarPlayNetworkWait {
    unsigned long began_, lastPoll_;
    bool available_, announced_, polled_;
public:
    explicit CarPlayNetworkWait(unsigned long now)
        : began_(now), lastPoll_(0), available_(false), announced_(false), polled_(false) {}
    void available() { available_=true; }
    bool waiting() const { return available_; }
    bool timedOut(unsigned long now) const {
        return (announced_ || !waiting()) && (unsigned long)(now-began_)>=60000;
    }
    std::string poll(unsigned long now, std::string (*lookup)()) {
        if(!waiting() || (polled_ && (unsigned long)(now-lastPoll_)<1000))return "";
        polled_=true;lastPoll_=now;
        std::string address;
        try { address=lookup(); }
        catch(const std::exception &e) {
            if(std::string(e.what())!="Waiting for receiver USB network IPv6 link-local address")throw;
            return "";
        }
        if(!address.empty()){
            if(!announced_)began_=now;
            announced_=true;available_=false;
        }
        return address;
    }
};
}
#endif
