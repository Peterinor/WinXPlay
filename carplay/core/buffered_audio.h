#ifndef MRN_BUFFERED_AUDIO_H
#define MRN_BUFFERED_AUDIO_H
#include "../interfaces/media.h"
#include <deque>
namespace cp {
// TCP buffered AAC. Retain submitted packets until the output clock consumes them,
// so pausing can requeue the unheard tail without growing an unbounded history.
class BufferedAudio {
    struct Packet {
        U32 time;
        Bytes payload;
        Packet(U32 t, const Bytes &b) : time(t), payload(b) {}
    };
    unsigned rate_;
    U64 connection_;
    AudioOutput *audio_;
    MediaFactory &factory_;
    std::deque<Packet> packets_;
    size_t bytes_, fed_;
    bool playing_, started_, anchored_;
    U32 anchorRtp_;
    U64 anchorNtp_;
    U64 retiredPlayed_;
    BufferedAudio(const BufferedAudio &);
    BufferedAudio &operator=(const BufferedAudio &);
    void resetOutput();
    void discardBefore(U32 timestamp);

  public:
    BufferedAudio(MediaFactory &, unsigned rate, U64 connection);
    ~BufferedAudio();
    bool hasCapacity() const;
    void receive(U32 timestamp, const Bytes &payload);
    void tick(U64 now);
    U32 position() const;
    Value control(const std::string &method, const Value &body, U64 now);
    Value anchor() const;
    Value feedback(U64 now, U32 milliseconds) const;
    U64 playedBytes() const;
};
} // namespace cp
#endif
