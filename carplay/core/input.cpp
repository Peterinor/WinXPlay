#include "input.h"
#include <algorithm>
#include <cmath>
namespace cp {
namespace {
const char* touchId="2a2a2a2a";
const char* knobId="2a2a2a2b";
const char* mediaId="2a2a2a2c";
const char* phoneId="2a2a2a2d";
void device(Value& list,const char* id,const char* name,const Bytes& descriptor) {
    Value hid=Value::dictionary();
    hid.dict["hidProductID"]=Value::num(1);hid.dict["hidVendorID"]=Value::num(2);
    hid.dict["hidCountryCode"]=Value::num(0);hid.dict["uuid"]=Value::str(id);
    hid.dict["name"]=Value::str(name);
    hid.dict["hidDescriptor"]=Value::blob(descriptor);list.array.push_back(hid);
}
bool coordinate(double v) { return v==v && v>=-1e9 && v<=1e9; }
}
Value inputDevices(unsigned width,unsigned height,bool touchscreen,bool controls) {
    Value list=Value::list();
    if(touchscreen) {
        require(width && height && width<=65535 && height<=65535,"Invalid touch dimensions");
        Bytes finger=unhex("050d0922a1020938750895018102150025010933750195018102950781030501262003093075109501810226e00109318102c0");
        unsigned axis=0;
        for(size_t i=0;i+2<finger.size();++i)if(finger[i]==0x26){
            put16(finger,i+1,U16(axis++==0?width:height),true);i+=2;
        }
        Bytes descriptor=unhex("050d0904a101");
        descriptor.insert(descriptor.end(),finger.begin(),finger.end());
        descriptor.insert(descriptor.end(),finger.begin(),finger.end());descriptor.push_back(0xc0);
        device(list,touchId,"WinXPlay Touchscreen",descriptor);
    }
    if(controls) {
        device(list,knobId,"WinXPlay Controller",unhex("05010908a1010509090115002501750195018102050c0a23020a2402950281029505810105010901a100093009311581257f750895028102c009381581257f750895018106c0"));
        device(list,mediaId,"WinXPlay Media",unhex("050c0901a10115002506050c0a00000ab0000ab1000acd000ab5000ab6000a9e02750895018100c0"));
        device(list,phoneId,"WinXPlay Telephone",unhex("050b0907a10115002511050b0900092009210926092f09b009b109b209b309b409b509b609b709b809b909ba09bb0507092a750895018100c0"));
    }
    return list;
}
InputQueue::InputQueue(unsigned w,unsigned h,bool t,bool c):width_(w),height_(h),
    touchEnabled_(t),controlsEnabled_(c),ready_(false),night_(false),nightKnown_(false),overflows_(0) {
    require(w && h && w<=65535 && h<=65535,"Invalid input dimensions");
    for(unsigned i=0;i<InputKeyCount;++i)keys_[i]=false;
    for(unsigned i=0;i<2;++i){down_[i]=false;x_[i]=y_[i]=0;}
}
Bytes InputQueue::contacts() const {
    Bytes result(12,0);
    for(unsigned i=0;i<2;++i){
        result[i*6]=U8(i);result[i*6+1]=down_[i]?1:0;
        put16(result,i*6+2,U16(std::floor(x_[i]*width_+0.5)),true);
        put16(result,i*6+4,U16(std::floor(y_[i]*height_+0.5)),true);
    }
    return result;
}
Bytes InputQueue::buttons(int x,int y,int wheel) const {
    return knob(keys_[InputSelect],keys_[InputHome],keys_[InputBack],x,y,wheel);
}
void InputQueue::hid(const char* id,const Bytes& report) {
    Value v=Value::dictionary();v.dict["type"]=Value::str("hidSendReport");
    v.dict["uuid"]=Value::str(id);v.dict["hidReport"]=Value::blob(report);pending_.push_back(v);
}
void InputQueue::command(const char* type,const char* name,const Value& value) {
    Value v=Value::dictionary(),params=Value::dictionary();
    v.dict["type"]=Value::str(type);if(name)params.dict[name]=value;
    v.dict["params"]=params;pending_.push_back(v);
}
void InputQueue::neutral() {
    bool siri=keys_[InputSiri];
    for(unsigned i=0;i<InputKeyCount;++i)keys_[i]=false;
    down_[0]=down_[1]=false;
    if(!ready_)return;
    if(touchEnabled_)hid(touchId,contacts());
    if(controlsEnabled_){
        hid(knobId,buttons());hid(mediaId,Bytes(1,0));hid(phoneId,Bytes(1,0));
        if(siri)command("requestSiri","siriAction",Value::num(3));
    }
}
bool InputQueue::reserve(unsigned count) {
    if(!ready_)return false;
    if(pending_.size()+count<=64)return true;
    ++overflows_;
    // A lost release is worse than a lost click. Reset every advertised input
    // after overload; do not replay old presses against a later screen.
    pending_.clear();neutral();
    if(nightKnown_)command("setNightMode","nightMode",Value::flag(night_));
    return false;
}
void InputQueue::ready(bool value) {
    pending_.clear();ready_=false;neutral();ready_=value;
    if(value && nightKnown_)command("setNightMode","nightMode",Value::flag(night_));
}
bool InputQueue::contact(unsigned slot,double x,double y,bool down) {
    if(!touchEnabled_ || !ready_ || slot>=2 || !coordinate(x) || !coordinate(y))return false;
    x=std::max(0.0,std::min(1.0,x));y=std::max(0.0,std::min(1.0,y));
    if(!down && !down_[slot])return true;
    bool move=down && down_[slot];
    if(move && !pending_.empty() && pending_.back().dict.count("uuid") &&
       pending_.back().at("uuid").string==touchId) {
        const Bytes& previous=pending_.back().at("hidReport").data;
        // Keep down/up edges; only replace another move with the same contacts.
        if(previous[1]==unsigned(down_[0]) && previous[7]==unsigned(down_[1]) &&
           pending_.back().dict.count("_move")) {
            x_[slot]=x;y_[slot]=y;pending_.back().dict["hidReport"]=Value::blob(contacts());return true;
        }
    }
    if(!reserve(1))return false;
    x_[slot]=x;y_[slot]=y;down_[slot]=down;hid(touchId,contacts());
    if(move)pending_.back().dict["_move"]=Value::flag(true);
    return true;
}
bool InputQueue::key(InputKey k,bool down) {
    if(!controlsEnabled_ || !ready_ || k<0 || k>=InputKeyCount)return false;
    if(keys_[k]==down)return true;
    if(!reserve(2))return false;
    keys_[k]=down;
    if(k<=InputBack)hid(knobId,buttons());
    else if(k==InputSiri)command("requestSiri","siriAction",Value::num(down?2:3));
    else if(down) {
        const unsigned indices[]={1,2,3,4,5,1,4};
        const char* id=k>=InputAnswer?phoneId:mediaId;
        hid(id,Bytes(1,U8(indices[k-InputPlay])));hid(id,Bytes(1,0));
    }
    return true;
}
bool InputQueue::navigate(int x,int y,int wheel) {
    if(!controlsEnabled_ || !reserve(2))return false;
    hid(knobId,buttons(x,y,wheel));hid(knobId,buttons());return true;
}
void InputQueue::cancel() { if(reserve(5))neutral(); }
void InputQueue::nightMode(bool night) {
    night_=night;nightKnown_=true;
    if(reserve(1))command("setNightMode","nightMode",Value::flag(night));
}
bool InputQueue::keyFrame() {
    if(!reserve(1))return false;
    command("forceKeyFrame",0,Value());return true;
}
void InputQueue::drain(std::vector<Value>& out) {
    out.clear();out.swap(pending_);
    for(size_t i=0;i<out.size();++i)out[i].dict.erase("_move");
}
}
