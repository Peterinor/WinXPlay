#pragma once
#include <windows.h>
#include <string>
#include "connection_labels.h"
#include "connection_font.h"
namespace wxui {
enum Phase { WaitingPhone, PreparingUsb, TrustPhone, NetworkLink, VideoStart, RetryLink, Failed, LockedPhone, Stopping, PausedPhase, DetectingPhase, DiagnosticPhase };
struct State {
    Phase phase;
    unsigned step;
    bool streaming;
    std::wstring detail;
    State():phase(WaitingPhone),step(0),streaming(false){}
    bool contains(const std::wstring& s,const wchar_t* part) const { return s.find(part)!=std::wstring::npos; }
    void stage(unsigned value) {
        if(streaming)return;
        step=value;
        phase=value==0?PreparingUsb:value==1?TrustPhone:value==2?NetworkLink:VideoStart;
        detail.clear();
    }
    void update(const std::wstring& s) {
        if(s==L"CarPlay video streaming"){streaming=true;return;}
        if(contains(s,L"PasswordProtected") || contains(s,L"keep it awake")) {phase=LockedPhone;step=1;}
        else if(contains(s,L"Trust") || contains(s,L"PairingDialogResponsePending")) {phase=TrustPhone;step=1;}
        else if(contains(s,L"IPv6") || contains(s,L"NCM")) {phase=NetworkLink;step=2;}
        else if(contains(s,L"waiting for CarPlay video") || contains(s,L"RECORD accepted")) {phase=VideoStart;step=3;}
        else if(contains(s,L"Connect and unlock") || contains(s,L"Waiting for iPhone USB") || contains(s,L"open error=55")) {phase=WaitingPhone;step=0;}
        else if(contains(s,L"Preparing") || contains(s,L"re-enumeration")) {phase=PreparingUsb;step=0;}
        else if(contains(s,L"closed") || contains(s,L"disconnected")) {phase=RetryLink;}
        else if(contains(s,L"failed") || contains(s,L"error") || contains(s,L"timeout") ||
                contains(s,L"missing") || contains(s,L"Missing") || contains(s,L"mismatch") || contains(s,L"rejected")) {phase=Failed;}
        // Listening and low-level progress are not proof that a phone is ready.
        detail=s;
    }
    const Label& title() const {
        switch(phase){case PausedPhase:return Paused;case DetectingPhase:return Detecting;case DiagnosticPhase:return DiagnosticReady;
        case PreparingUsb:return Preparing;case TrustPhone:return Trust;
        case NetworkLink:return Network;case VideoStart:return Video;case RetryLink:return Retry;
        case Failed:return Error;case LockedPhone:return Locked;case Stopping:return Stopped;default:return Waiting;}
    }
    const Label& hint() const {
        switch(phase){case PausedPhase:return PausedHint;case DetectingPhase:return DetectingHint;case DiagnosticPhase:return DiagnosticHint;
        case PreparingUsb:return PreparingHint;case TrustPhone:return TrustHint;
        case NetworkLink:return NetworkHint;case VideoStart:return VideoHint;case RetryLink:return RetryHint;
        case Failed:return ErrorHint;case LockedPhone:return LockedHint;case Stopping:return StoppedHint;default:return WaitingHint;}
    }
};
// One 800x480 logical canvas for both desktop Windows and Windows CE.
// Unicode text uses the device font through GDI, including dynamic details.
class Canvas {
    HDC dc_; const ConnectionFont& font_; int scale_,left_,top_;
    int x(int v) const {return left_+v*scale_/800;}
    int y(int v) const {return top_+v*scale_/800;}
  public:
    Canvas(HDC dc,const RECT& r,const ConnectionFont& font):dc_(dc),font_(font) {
        scale_=r.right-r.left;
        if((r.bottom-r.top)*800/480<scale_)scale_=(r.bottom-r.top)*800/480;
        if(scale_<1)scale_=1;
        left_=r.left+((r.right-r.left)-scale_)/2;
        top_=r.top+((r.bottom-r.top)-480*scale_/800)/2;
    }
    void box(int l,int t,int w,int h,COLORREF color) {
        RECT r={x(l),y(t),x(l+w),y(t+h)};HBRUSH b=CreateSolidBrush(color);
        FillRect(dc_,&r,b);DeleteObject(b);
    }
    void round(int l,int t,int w,int h,int radius,COLORREF fill,COLORREF stroke) {
        HBRUSH b=CreateSolidBrush(fill);HPEN p=CreatePen(PS_SOLID,1,stroke);
        HGDIOBJ oldB=SelectObject(dc_,b),oldP=SelectObject(dc_,p);
        RoundRect(dc_,x(l),y(t),x(l+w),y(t+h),radius*scale_/800,radius*scale_/800);
        SelectObject(dc_,oldP);SelectObject(dc_,oldB);DeleteObject(p);DeleteObject(b);
    }
    void draw(const wchar_t* value,int size,int l,int t,int w,int h,COLORREF color,
              UINT format=DT_LEFT|DT_WORDBREAK|DT_NOPREFIX) {
        HFONT f=font_.create(size*scale_/800,size>=30?FW_MEDIUM:FW_NORMAL);
        HGDIOBJ old=f?SelectObject(dc_,f):0;
        COLORREF oldColor=SetTextColor(dc_,color);int oldMode=SetBkMode(dc_,TRANSPARENT);
        RECT r={x(l),y(t),x(l+w),y(t+h)};
        DrawTextW(dc_,value,-1,&r,format);
        SetTextColor(dc_,oldColor);SetBkMode(dc_,oldMode);
        if(f){SelectObject(dc_,old);DeleteObject(f);}
    }
    void text(const Label& label,int l,int t,COLORREF color,COLORREF) {
        draw(font_.chinese()?label.text:label.english,label.size,l,t,760-l,label.size*2+8,color);
    }
    void detail(const std::wstring& raw,int l,int t,int w,int h,COLORREF color) {
        std::wstring s=raw.substr(0,180);
        for(size_t i=0;i<s.size();++i)if(s[i]<32)s[i]=L' ';
        draw(s.c_str(),14,l,t,w,h,color);
    }
};
inline void paint(HDC dc,const RECT& r,const State& state,unsigned tick,const ConnectionFont& font,const cp::DeviceUiProfile& ui) {
    const COLORREF bg=RGB(10,17,29),card=RGB(19,30,46),line=RGB(42,57,76);
    const COLORREF bright=RGB(233,241,250),muted=RGB(151,171,194),cyan=RGB(68,212,221),amber=RGB(245,184,83);
    HBRUSH b=CreateSolidBrush(bg);FillRect(dc,&r,b);DeleteObject(b);
    Canvas c(dc,r,font);
    c.text(Brand,40,27,bright,bg);c.draw(ui.caption,16,625,40,135,40,muted);
    c.box(40,87,720,1,line);
    c.round(40,112,720,192,20,card,line);
    COLORREF accent=(state.phase==Failed || state.phase==LockedPhone || state.phase==TrustPhone)?amber:cyan;
    c.round(76,148,66,111,12,card,accent);
    c.box(98,157,23,3,accent);c.box(103,246,12,3,accent);
    c.box(158,201,4,4,muted);c.box(169,201,4,4,muted);c.box(180,201,4,4,accent);
    c.text(state.title(),204,151,bright,card);
    c.text(state.hint(),204,208,muted,card);
    // Indeterminate dots, never an invented percentage or countdown.
    if(state.phase!=Stopping && state.phase!=PausedPhase && state.phase!=DiagnosticPhase)
        for(unsigned i=0;i<3;++i)c.round(206+i*14,270,5,5,5,i==tick%3?accent:line,i==tick%3?accent:line);
    const Label* steps[]={&StepUsb,&StepTrust,&StepLink,&StepVideo};
    if(state.phase==DiagnosticPhase) {
        c.draw(state.detail.c_str(),16,42,318,714,106,muted,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX|DT_END_ELLIPSIS);
    } else if(state.phase!=PausedPhase && state.phase!=DetectingPhase)for(unsigned i=0;i<4;++i) {
        int x=86+i*181;
        COLORREF color=i<state.step?cyan:i==state.step?accent:line;
        c.round(x,335,12,12,12,color,color);
        if(i<3)c.box(x+22,340,146,2,i<state.step?cyan:line);
        c.text(*steps[i],x-29,358,i<=state.step?bright:muted,bg);
    }
    if(state.phase==Failed)c.detail(state.detail,42,397,714,34,amber);
    c.draw(font.chinese()?ui.connectionHint:ui.connectionHintEnglish,16,40,437,720,40,muted);
}
}
