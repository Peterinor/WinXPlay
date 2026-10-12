#include "protocol.h"
namespace cp {
LinkNegotiator::LinkNegotiator()
    : started_(false), synchronized_(false), ready_(false), peerSequence_(0), localSequence_(99),
      start_(0), retry_(0), now_(0), sentAt_(0), sendRetries_(0), peerMtu_(0) {}
Bytes LinkNegotiator::syn() const {
    LinkFrame f;
    f.control = 0x80;
    f.sequence = 99;
    f.ack = peerSequence_;
    f.session = 0;
    // v1, window 30, MTU 65535, retry 4000ms, ACK 500ms, retry 4, ACK threshold 3,
    // control session 10/v1. Only this implemented session is advertised.
    f.body = unhex("011effff0fa001f404030a0001");
    return linkFrame(f);
}
void LinkNegotiator::start(U64 now) {
    require(!started_, "Link already started");
    started_ = true;
    start_ = now;
    now_ = now;
    retry_ = now + 500;
    output_ = unhex("ff550200ee10");
    Bytes s = syn();
    output_.insert(output_.end(), s.begin(), s.end());
}
void LinkNegotiator::ack() {
    LinkFrame f;
    f.control = 0x40;
    f.sequence = localSequence_;
    f.ack = peerSequence_;
    f.session = 0;
    Bytes b = linkFrame(f);
    require(output_.size() + b.size() <= 65536, "Link output queue limit");
    output_.insert(output_.end(), b.begin(), b.end());
}
void LinkNegotiator::feed(const Bytes &chunk) {
    require(started_ && chunk.size() <= 65535 && pending_.size() <= 131070 - chunk.size(),
            "Link input buffer limit");
    pending_.insert(pending_.end(), chunk.begin(), chunk.end());
    while (pending_.size() >= 2) {
        if (pending_[0] == 0xff && pending_[1] == 0x55) {
            if (pending_.size() < 6)
                return;
            require(slice(pending_, 0, 6) == unhex("ff550200ee10"), "Invalid iAP2 marker");
            pending_.erase(pending_.begin(), pending_.begin() + 6);
            continue;
        }
        if (pending_[0] != 0xff || pending_[1] != 0x5a) {
            pending_.erase(pending_.begin());
            continue;
        }
        if (pending_.size() < 9)
            return;
        size_t n = be16(pending_, 2);
        require(n >= 9, "Invalid link length");
        if (pending_.size() < n)
            return;
        LinkFrame f = parseLink(slice(pending_, 0, n));
        pending_.erase(pending_.begin(), pending_.begin() + n);
        require((f.control & 0x10) == 0, "iPhone reset iAP2 link");
        if (f.control & 0x80) {
            require(f.body.size() >= 13 && f.body[0] == 1 && be16(f.body, 2) > 10 &&
                        (f.body.size() - 10) % 3 == 0,
                    "Invalid peer synchronization");
            bool control = false;
            for (size_t i = 10; i < f.body.size(); i += 3)
                if (f.body[i] == 10 && f.body[i + 1] == 0 && f.body[i + 2] == 1)
                    control = true;
            require(control, "Peer omitted compatible control session");
            peerSequence_ = f.sequence;
            peerMtu_ = be16(f.body, 2);
            synchronized_ = true;
            ack();
        }
        if ((f.control & 0x40) && synchronized_ && f.ack == 99)
            ready_ = true;
        if ((f.control & 0x40) && !unacknowledged_.empty() && f.ack == localSequence_)
            unacknowledged_.clear();
        if ((f.control & ~0x40) == 0 && !f.body.empty()) {
            require(ready_, "Control data before link ready");
            U8 distance = U8(f.sequence - peerSequence_);
            if (distance == 0) {
                ack();
                continue;
            }
            require(distance == 1, "Out-of-order bring-up frame");
            peerSequence_ = f.sequence;
            ack();
            if (f.session == 10) {
                std::vector<Frame> messages = csm_.feed(f.body);
                require(messages_.size() + messages.size() <= 32, "Bring-up message limit");
                messages_.insert(messages_.end(), messages.begin(), messages.end());
            }
        }
    }
}
void LinkNegotiator::tick(U64 now) {
    require(started_ && now >= start_, "Invalid monotonic clock");
    require(ready_ || now - start_ < 10000, "iAP2 negotiation timed out");
    now_ = now;
    if (!unacknowledged_.empty() && now - sentAt_ >= 4000) {
        require(sendRetries_ < 4, "Authentication link acknowledgement timed out");
        require(output_.size() + unacknowledged_.size() <= 65536, "Link retry output limit");
        output_.insert(output_.end(), unacknowledged_.begin(), unacknowledged_.end());
        sentAt_ = now;
        ++sendRetries_;
    }
    if (!ready_ && now >= retry_) {
        Bytes s = syn();
        require(output_.size() + s.size() <= 65536, "Link retry queue limit");
        output_.insert(output_.end(), s.begin(), s.end());
        retry_ = now + 500;
    }
}
void LinkNegotiator::send(const Frame &message) {
    require(ready_ && unacknowledged_.empty(), "Authentication link not ready or awaiting ACK");
    LinkFrame f;
    f.control = 0x40;
    f.sequence = U8(localSequence_ + 1);
    f.ack = peerSequence_;
    f.session = 10;
    f.body = csm(message.id, message.body);
    Bytes data = linkFrame(f);
    require(data.size() <= peerMtu_ && output_.size() + data.size() <= 65536,
            "Authentication message exceeds negotiated link limit");
    localSequence_ = f.sequence;
    unacknowledged_ = data;
    output_.insert(output_.end(), data.begin(), data.end());
    sentAt_ = now_;
    sendRetries_ = 0;
}
Bytes LinkNegotiator::takeOutput() {
    Bytes b;
    output_.swap(b);
    return b;
}
std::vector<Frame> LinkNegotiator::takeMessages() {
    std::vector<Frame> out;
    out.swap(messages_);
    return out;
}
} // namespace cp
