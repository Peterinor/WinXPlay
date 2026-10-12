// Render the exact CE painter with Win32 GDI, no USB or phone access.
#include "../../platform/common/connection_ui.h"
#include <cstdio>
#include <cassert>
int wmain(int argc,wchar_t** argv) {
    wxui::State s;
    s.update(L"Waiting for iPhone USB driver CMX1: open error=55");assert(s.phase==wxui::WaitingPhone);
    s.stage(1);s.update(L"Accept Trust on iPhone; pairing will retry automatically");assert(s.phase==wxui::TrustPhone);
    s.update(L"Unlock iPhone and keep it awake; pairing will retry automatically");assert(s.phase==wxui::LockedPhone);
    s.update(L"Waiting for receiver USB network IPv6 link-local address");assert(s.phase==wxui::NetworkLink);
    s.update(L"Receiver USB network ready; waiting for CarPlay video");assert(s.phase==wxui::VideoStart);
    s.update(L"Receiver error: missing plist key");assert(s.phase==wxui::Failed);
    s.update(L"CarPlay video streaming");s.stage(0);assert(s.streaming && s.phase==wxui::Failed);
    HDC dc=CreateCompatibleDC(0);assert(dc);
    // Use the actual OEM font extracted for local validation only. Never package it.
    SelectObject(dc,GetStockObject(OEM_FIXED_FONT));
    wxui::ConnectionFont font;
    assert(argc==2);
    cp::DeviceUiProfile ui=cp::deviceProfile().ui;
#ifdef WXUI_GENERIC_TEST
    assert(ui.fontCount==0 && ui.fonts==0);
    assert(wcsstr(ui.caption,L"MRN")==0 && wcsstr(ui.connectionHintEnglish,L"BAND")==0);
    font.initialize(dc,ui);assert(font.loadedPath().empty());
    cp::UiFontResource resource={argv[1],L"Microsoft YaHei",L"\u5fae\u8f6f\u96c5\u9ed1"};
#else
    assert(ui.fontCount==2 && ui.fonts);
    cp::UiFontResource resource=ui.fonts[0];resource.path=argv[1];
#endif
    // Only remap the filesystem path for local QA; production uses the profile.
    ui.fonts=&resource;ui.fontCount=1;
    font.initialize(dc,ui);
    assert(font.chinese() && font.face()==L"Microsoft YaHei" && font.loadedPath()==argv[1]);
    HFONT probe=font.create(20);HGDIOBJ oldFont=SelectObject(dc,probe);
    wchar_t face[LF_FACESIZE]={0};GetTextFaceW(dc,LF_FACESIZE,face);
    assert(_wcsicmp(face,L"Microsoft YaHei")==0 || _wcsicmp(face,L"\u5fae\u8f6f\u96c5\u9ed1")==0);
    const wchar_t* sample=L"\u6b63\u5728\u8fde\u63a5\u624b\u673a\u6b4c\u8bcd\uff1a\u660e\u6708\u51e0\u65f6\u6709";
    WORD glyphs[64]={0};size_t count=wcslen(sample);
    assert(GetGlyphIndicesW(dc,sample,int(count),glyphs,GGI_MARK_NONEXISTING_GLYPHS)!=GDI_ERROR);
    for(size_t i=0;i<count;++i)assert(glyphs[i]!=0xffff);
    SelectObject(dc,oldFont);DeleteObject(probe);
    DWORD before=0;
    const int sizes[][2]={{800,480},{480,272},{1280,720}};
    // GDI initializes font/cache handles lazily. Warm every size first, then
    // repeat the same workload and check that the handle count stays flat.
    for(unsigned pass=0;pass<2;++pass) {
    if(pass)before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(unsigned size=0;size<3;++size) {
        int w=sizes[size][0],h=sizes[size][1],pitch=(w*3+3)&~3;
        BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=24;
        void* pixels=0;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,0,0);assert(bitmap && pixels);
        HGDIOBJ old=SelectObject(dc,bitmap);RECT r={0,0,w,h};
        for(unsigned phase=0;phase<=wxui::DiagnosticPhase;++phase) {
            s.phase=wxui::Phase(phase);s.step=(phase==wxui::NetworkLink?2:phase==wxui::VideoStart?3:phase==wxui::TrustPhone?1:0);
            s.detail=sample; // Dynamic Unicode, not one of the fixed UI labels.
            for(unsigned tick=0;tick<4;++tick)wxui::paint(dc,r,s,tick,font,ui);
            GdiFlush();
            BITMAPFILEHEADER file={0};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
            file.bfSize=file.bfOffBits+pitch*h;
            char path[100];
#ifdef WXUI_GENERIC_TEST
            sprintf(path,"connection-generic-%ux%u-%u.bmp",w,h,phase);
#else
            sprintf(path,"connection-%ux%u-%u.bmp",w,h,phase);
#endif
            FILE* out=fopen(path,"wb");assert(out);
            fwrite(&file,1,sizeof(file),out);fwrite(&info.bmiHeader,1,sizeof(BITMAPINFOHEADER),out);
            fwrite(pixels,1,pitch*h,out);fclose(out);
        }
        SelectObject(dc,old);DeleteObject(bitmap);
    }
    }
    DWORD after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    printf("GDI handles after warmup=%lu after repeated paints=%lu\n",before,after);fflush(stdout);
    assert(after==before);
    font.release();assert(font.loadedPath().empty());
    // Missing OEM path falls back to installed fonts without pretending it loaded.
    resource.path=L"Z:\\WinXPlay-missing-font.ttf";
    font.initialize(dc,ui);assert(font.loadedPath().empty());
    // A missing first device resource must not hide a usable later candidate.
    cp::UiFontResource candidates[]={resource,resource};candidates[1].path=argv[1];
    ui.fonts=candidates;ui.fontCount=2;
    font.initialize(dc,ui);assert(font.loadedPath()==argv[1]);
    font.release();
    DeleteDC(dc);puts("OEM font load/release, Unicode glyphs, fallback, CE state, 36 views, 288 paints: passed; GDI handles stable.");
    return 0;
}
