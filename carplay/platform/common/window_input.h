#ifndef WINXPLAY_WINDOW_INPUT_H
#define WINXPLAY_WINDOW_INPUT_H
#include "../../core/receiver.h"
#include "input_messages.h"
namespace cp {
// The caller holds its receiver-lifetime lock. Only this UI thread owns state.
// CE touch drivers emit mouse messages; native Windows touch keeps two contacts.
class WindowInput {
    Receiver* bound_;
    bool mouse_, night_;
    int wheel_;
    double x_,y_;
#ifndef UNDER_CE
    struct TouchData {
        LONG x,y;HANDLE source;DWORD id,flags,mask,time;ULONG_PTR extra;DWORD width,height;
    };
    typedef BOOL (WINAPI *RegisterTouch)(HWND,ULONG);
    typedef BOOL (WINAPI *ReadTouch)(HANDLE,UINT,TouchData*,int);
    typedef BOOL (WINAPI *CloseTouch)(HANDLE);
    ReadTouch readTouch_;CloseTouch closeTouch_;
    bool touchRegistered_, nativeDown_[2];DWORD nativeId_[2];
    void initializeTouch(HWND h) {
        if(touchRegistered_)return;
        touchRegistered_=true;
        HMODULE user=GetModuleHandleW(L"user32.dll");
        RegisterTouch reg=(RegisterTouch)GetProcAddress(user,"RegisterTouchWindow");
        readTouch_=(ReadTouch)GetProcAddress(user,"GetTouchInputInfo");
        closeTouch_=(CloseTouch)GetProcAddress(user,"CloseTouchInputHandle");
        if(!reg || !readTouch_ || !closeTouch_ || !reg(h,0))readTouch_=0;
    }
    bool touches(HWND h,WPARAM wp,LPARAM lp,const RECT& r,Receiver* receiver) {
        HANDLE handle=(HANDLE)lp;unsigned count=LOWORD(wp);
        TouchData data[16];
        if(!readTouch_ || !closeTouch_)return false;
        if(count && count<=16 && readTouch_(handle,count,data,sizeof(TouchData))) {
            for(unsigned i=0;i<count;++i) {
                TouchData& t=data[i];int slot=-1;
                for(unsigned j=0;j<2;++j)if(nativeDown_[j] && nativeId_[j]==t.id)slot=int(j);
                POINT p={t.x/100,t.y/100};ScreenToClient(h,&p);
                if((t.flags&2) && slot<0 && receiver && PtInRect(&r,p)) {
                    for(unsigned j=0;j<2;++j)if(!nativeDown_[j]){slot=int(j);nativeId_[j]=t.id;break;}
                }
                if(slot<0 || !receiver || r.right<=r.left || r.bottom<=r.top)continue;
                bool down=(t.flags&4)==0;
                nativeDown_[slot]=down;
                receiver->touchContact(unsigned(slot),double(p.x-r.left)/(r.right-r.left),
                    double(p.y-r.top)/(r.bottom-r.top),down);
            }
        } else if(receiver) cancel(receiver);
        closeTouch_(handle);return true;
    }
#endif
    void cancel(Receiver* receiver) {
        mouse_=false;wheel_=0;
#ifndef UNDER_CE
        nativeDown_[0]=nativeDown_[1]=false;
#endif
        if(receiver)receiver->cancelInput();
    }
    static bool mappedKey(WPARAM key,InputKey& out) {
        switch(key) {
        case VK_RETURN:out=InputSelect;break;
        case VK_HOME:out=InputHome;break;
        case VK_BACK:out=InputBack;break;
        case VK_SPACE:case 0xb3:out=InputPlayPause;break;
        case 0xb0:out=InputNext;break;
        case 0xb1:out=InputPrevious;break;
        case 0xb2:out=InputPause;break;
        case VK_F6:out=InputAnswer;break;
        case VK_F7:out=InputHangUp;break;
        case VK_F8:out=InputSiri;break;
        default:return false;
        }
        return true;
    }
  public:
    WindowInput():bound_(0),mouse_(false),night_(false),wheel_(0),x_(0),y_(0)
#ifndef UNDER_CE
        ,readTouch_(0),closeTouch_(0),touchRegistered_(false)
#endif
    {
#ifndef UNDER_CE
        nativeDown_[0]=nativeDown_[1]=false;nativeId_[0]=nativeId_[1]=0;
#endif
    }
    void setNightMode(bool value) { night_=value; }
    bool handle(HWND h,UINT msg,WPARAM wp,LPARAM lp,Receiver* receiver,const RECT& rect,bool captureMouse=true) {
        if(bound_!=receiver) {
            // Never dereference bound_: the previous session may be destroyed.
            cancel(0);bound_=receiver;
            if(receiver)receiver->setNightMode(night_);
        }
#ifndef UNDER_CE
        if(msg==WM_CREATE)initializeTouch(h);
        if(msg==0x0240)return touches(h,wp,lp,rect,receiver); // WM_TOUCH
#endif
        if(msg==WM_CAPTURECHANGED && !mouse_)return false;
        if(msg==WINXPLAY_INPUT_VISIBLE) {
            if(wp<=1){cancel(receiver);ShowWindow(h,wp?SW_RESTORE:SW_MINIMIZE);if(wp)SetForegroundWindow(h);}
            return true;
        }
        if(msg>=WINXPLAY_INPUT_CONTACT && msg<=WINXPLAY_INPUT_KEYFRAME) {
            if(msg==WINXPLAY_INPUT_NIGHT) {
                if(wp<=1){night_=wp!=0;if(receiver)receiver->setNightMode(night_);}
                return true;
            }
            if(!receiver)return true;
            switch(msg) {
            case WINXPLAY_INPUT_CONTACT:
                if((wp&~WPARAM(0x101))==0)receiver->touchContact(unsigned(wp&1),
                    double(LOWORD(lp))/65535.0,double(HIWORD(lp))/65535.0,(wp&0x100)!=0);
                break;
            case WINXPLAY_INPUT_KEY:
                if(wp<InputKeyCount && (lp==0 || lp==1))receiver->inputKey(InputKey(wp),lp!=0);
                break;
            case WINXPLAY_INPUT_NAVIGATE:receiver->navigate(short(LOWORD(lp)),short(HIWORD(lp)),int(wp));break;
            case WINXPLAY_INPUT_CANCEL:cancel(receiver);break;
            case WINXPLAY_INPUT_KEYFRAME:receiver->requestKeyFrame();break;
            }
            return true;
        }
        if(msg==0x0319 && receiver) { // WM_APPCOMMAND (including multimedia keyboards)
            InputKey key;
            switch(HIWORD(lp)&0xfff) {
            case 11:key=InputNext;break;case 12:key=InputPrevious;break;
            case 13:case 47:key=InputPause;break;case 14:key=InputPlayPause;break;
            case 46:key=InputPlay;break;default:return false;
            }
            receiver->inputKey(key,true);receiver->inputKey(key,false);return true;
        }
        if(msg==WM_KILLFOCUS || msg==WM_CANCELMODE || msg==WM_CAPTURECHANGED ||
           msg==WM_SIZE || msg==WM_CLOSE) {
            cancel(receiver);
            if(GetCapture()==h)ReleaseCapture();
            return false;
        }
        if(msg==WM_KEYDOWN || msg==WM_KEYUP) {
            bool down=msg==WM_KEYDOWN;
            if(!receiver)return false;
            InputKey key;
            if(mappedKey(wp,key)) {
                if(!down || !(lp&(1L<<30)))receiver->inputKey(key,down);
                return true;
            }
            if(wp==VK_LEFT || wp==VK_RIGHT || wp==VK_UP || wp==VK_DOWN) {
                if(down)receiver->navigate(wp==VK_LEFT?-1:(wp==VK_RIGHT?1:0),
                    wp==VK_UP?-1:(wp==VK_DOWN?1:0),0);
                return true;
            }
            if(wp==VK_F9){if(down && !(lp&(1L<<30))){night_=!night_;receiver->setNightMode(night_);}return true;}
            if(wp==VK_F5){if(down && !(lp&(1L<<30)))receiver->requestKeyFrame();return true;}
            return false;
        }
        if(msg==WM_MOUSEWHEEL) {
            if(!receiver)return false;
            wheel_+=short(HIWORD(wp));int steps=wheel_/120;wheel_%=120;
            if(steps)receiver->navigate(0,0,-steps);
            return true;
        }
        if(msg!=WM_LBUTTONDOWN && msg!=WM_LBUTTONUP && msg!=WM_MOUSEMOVE)return false;
#ifndef UNDER_CE
        // Suppress mouse messages synthesized from contacts handled by WM_TOUCH.
        if(readTouch_ && (GetMessageExtraInfo() & 0xffffff00)==0xff515700)return true;
#endif
        if(!receiver || rect.right<=rect.left || rect.bottom<=rect.top)return true;
        POINT p={short(LOWORD(lp)),short(HIWORD(lp))};
        if(msg==WM_LBUTTONDOWN) {
            if(!PtInRect(&rect,p))return true;
            mouse_=true;if(captureMouse)SetCapture(h);
        } else if(!mouse_)return true;
        x_=double(p.x-rect.left)/(rect.right-rect.left);
        y_=double(p.y-rect.top)/(rect.bottom-rect.top);
        if(msg==WM_LBUTTONUP)mouse_=false;
        receiver->touchContact(0,x_,y_,mouse_);
        // Up must be queued before ReleaseCapture emits WM_CAPTURECHANGED.
        if(captureMouse && msg==WM_LBUTTONUP && GetCapture()==h)ReleaseCapture();
        return true;
    }
};
}
#endif
