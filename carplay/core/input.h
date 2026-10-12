#ifndef WINXPLAY_INPUT_H
#define WINXPLAY_INPUT_H
#include "protocol.h"
namespace cp {
enum InputKey {
    InputSelect=0, InputHome=1, InputBack=2, InputPlay=3, InputPause=4, InputPlayPause=5,
    InputNext=6, InputPrevious=7, InputAnswer=8, InputHangUp=9, InputSiri=10, InputKeyCount=11
};
// Two stable contact slots; platform drivers translate their contact IDs to 0/1.
// Coordinates are normalized to the displayed image, not the whole window.
Value inputDevices(unsigned width, unsigned height, bool touchscreen, bool controls);
class InputQueue {
    unsigned width_, height_;
    bool touchEnabled_, controlsEnabled_, ready_, keys_[InputKeyCount], down_[2];
    double x_[2], y_[2];
    bool night_, nightKnown_;
    unsigned overflows_;
    std::vector<Value> pending_;
    Bytes contacts() const;
    Bytes buttons(int x=0,int y=0,int wheel=0) const;
    void neutral();
    bool reserve(unsigned count);
    void hid(const char* uuid,const Bytes& report);
    void command(const char* type,const char* name,const Value& value);
  public:
    InputQueue(unsigned width,unsigned height,bool touchEnabled=true,bool controlsEnabled=true);
    void ready(bool value);
    bool contact(unsigned slot,double x,double y,bool down);
    bool key(InputKey key,bool down);
    bool navigate(int x,int y,int wheel);
    void cancel();
    void nightMode(bool night);
    bool keyFrame();
    void drain(std::vector<Value>& out);
    unsigned overflows() const { return overflows_; }
    unsigned pending() const { return unsigned(pending_.size()); }
};
}
#endif
