#pragma once
#include <windows.h>
#include <string>
#include "../../devices/profile.h"

namespace wxui {
// Own only our AddFontResource reference, never unload an OEM-owned reference.
// UI-thread lifetime: initialize once before painting, release after the window.
class ConnectionFont {
    std::wstring loaded_, face_;
    bool chinese_;
    ConnectionFont(const ConnectionFont&);
    ConnectionFont& operator=(const ConnectionFont&);
    static int CALLBACK found(const LOGFONTW*,const TEXTMETRICW*,DWORD,LPARAM data) {
        *reinterpret_cast<bool*>(data)=true;return 0;
    }
    bool select(HDC dc,const wchar_t* name) {
        bool exists=false;
        EnumFontFamiliesW(dc,name,found,reinterpret_cast<LPARAM>(&exists));
        if(exists){face_=name;chinese_=true;}
        return exists;
    }
    bool load(HDC dc,const wchar_t* path,const wchar_t* family,const wchar_t* localized=0) {
        if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES || !AddFontResourceW(path))return false;
        if(select(dc,family) || (localized && select(dc,localized))){loaded_=path;PostMessageW(HWND_BROADCAST,WM_FONTCHANGE,0,0);return true;}
        RemoveFontResourceW(path);return false;
    }
  public:
    ConnectionFont():chinese_(false){}
    ~ConnectionFont(){release();}
    void release() {
        if(!loaded_.empty()){RemoveFontResourceW(loaded_.c_str());loaded_.clear();PostMessageW(HWND_BROADCAST,WM_FONTCHANGE,0,0);}
        face_.clear();chinese_=false;
    }
    void initialize(HDC dc,const cp::DeviceUiProfile& ui) {
        release();
        for(unsigned i=0;i<ui.fontCount;++i){
            const cp::UiFontResource& font=ui.fonts[i];
            if(load(dc,font.path,font.family,font.localizedFamily))return;
        }
        const wchar_t* families[]={L"Microsoft YaHei",L"\u5fae\u8f6f\u96c5\u9ed1",L"SimHei",L"SimSun",L"NSimSun",L"Noto Sans SC"};
        for(unsigned i=0;i<sizeof(families)/sizeof(families[0]);++i)if(select(dc,families[i]))return;
#ifdef UNDER_CE
        if(load(dc,L"\\Windows\\simsun.ttc",L"SimSun"))return;
#endif
        // Keep the connection instructions readable on devices without CJK fonts.
        face_=L"Tahoma";
    }
    bool chinese() const{return chinese_;}
    const std::wstring& face() const{return face_;}
    const std::wstring& loadedPath() const{return loaded_;}
    HFONT create(int pixels,int weight=FW_NORMAL) const {
        LOGFONTW lf={0};lf.lfHeight=-(pixels>0?pixels:1);lf.lfWeight=weight;
        lf.lfCharSet=DEFAULT_CHARSET;lf.lfQuality=ANTIALIASED_QUALITY;
        for(unsigned i=0;i<face_.size() && i<LF_FACESIZE-1;++i)lf.lfFaceName[i]=face_[i];
        return CreateFontIndirectW(&lf);
    }
};
}
