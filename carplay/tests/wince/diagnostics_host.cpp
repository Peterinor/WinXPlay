// Run the CE diagnostic implementation on Windows file/thread APIs with synthetic inputs.
#include "../../platform/common/diagnostics.h"
#include <sstream>
#include <iostream>
#include <fstream>
namespace cp {
static std::wstring root;
std::wstring executableDirectory(){return root;}
std::string utf8(const std::wstring &w){
    int n=WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),0,0,0,0);
    std::string s(n,' ');if(n)WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),&s[0],n,0,0);return s;
}
Bytes bytes(const std::string &s){return Bytes(s.begin(),s.end());}
void writeFile(const std::wstring &p,const Bytes &b){
    HANDLE f=CreateFileW(p.c_str(),GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);DWORD n=0;
    if(f==INVALID_HANDLE_VALUE)throw std::runtime_error("create");
    BOOL ok=b.empty() || WriteFile(f,&b[0],DWORD(b.size()),&n,0);CloseHandle(f);
    if(!ok || n!=b.size())throw std::runtime_error("write");
}
#if WINXPLAY_ENABLE_LOGS
#include "../../platform/wince/diagnostics.inc"
#endif
}
#if !WINXPLAY_ENABLE_LOGS
namespace driverLogTest {
#include "../../platform/wince/usb_driver/trace.h"
}
namespace bridgeLogTest {
#include "../../../firmware/mrn/bridge/log.h"
}
#endif
using namespace cp;
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static std::string read(const wchar_t *name){
    std::ifstream f((root+L"\\"+name).c_str(),std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
}
static DWORD WINAPI writer(void *ctx){
    for(unsigned i=0;i<100;++i){std::ostringstream s;s<<size_t(ctx)<<":"<<i;diagnosticEvent("CONCURRENT",s.str());}return 0;
}
int main(){
    try{
        wchar_t path[MAX_PATH];GetModuleFileNameW(0,path,MAX_PATH);root=path;root=root.substr(0,root.find_last_of(L"\\"));
        wchar_t suffix[80];wsprintfW(suffix,L"\\runtime-%lu-%lu",GetCurrentProcessId(),GetTickCount());root+=suffix;
        check(CreateDirectoryW(root.c_str(),0)!=FALSE,"test directory");
#if !WINXPLAY_ENABLE_LOGS
        SetLastError(1234);
        diagnosticEvent("MUST_NOT_WRITE","silent release");
        writeDiagnosticFile(root+L"\\snapshot.plist",bytes("diagnostic"));
        driverLogTest::usbTrace("MUST_NOT_WRITE");
        bridgeLogTest::MrnOpenLog(L"MUST_NOT_OPEN");bridgeLogTest::MrnLog(L"MUST_NOT_WRITE");
        check(GetLastError()==1234,"disabled logging changed error");
        check(GetFileAttributesW((root+L"\\connection-trace.log").c_str())==INVALID_FILE_ATTRIBUTES,"release trace created");
        check(GetFileAttributesW((root+L"\\snapshot.plist").c_str())==INVALID_FILE_ATTRIBUTES,"release snapshot created");
        check(bridgeLogTest::mrnLog==INVALID_HANDLE_VALUE,"release bridge opened log");
        writeFile(root+L"\\persistent.dat",bytes("identity/config must still persist"));
        check(read(L"persistent.dat")=="identity/config must still persist","release disabled persistent storage");
        std::cout<<"PASS: release event/snapshot/USB/bridge logs are no-ops; persistent storage and errors preserved\n";
        return 0;
#else
        SetLastError(1234);diagnosticEvent("PAIR_RESPONSE","PairingDialogResponsePending\ninjected");
        check(GetLastError()==1234,"logging changed caller error");
        diagnosticEvent("PAIR_STORE_WRITE_VERIFIED","active host_tag=synthetic");
        diagnosticEvent("ATTEMPT_FAILED","TLS failed before unplug");
        diagnosticEvent("ATTEMPT_FAILED","CMX1 open error=55 after unplug");
        std::string log=read(L"connection-trace.log");
        check(log.find("TLS failed before unplug")!=std::string::npos && log.find("CMX1 open error=55 after unplug")!=std::string::npos,"unplug overwrote failure");
        check(read(L"connection-trace.log").find("Pending injected")!=std::string::npos,"newline not sanitized");
        writeFile(root+L"\\connection-trace.log",bytes(std::string(512*1024,'X')));
        diagnosticEvent("ROTATED","new generation");
        check(read(L"connection-trace.previous.log").size()==512*1024,"rotation lost previous evidence");
        check(read(L"connection-trace.log").find("ROTATED")!=std::string::npos,"rotation lost new evidence");
        HANDLE threads[4];for(size_t i=0;i<4;++i)threads[i]=CreateThread(0,0,writer,(void*)i,0,0);
        check(WaitForMultipleObjects(4,threads,TRUE,30000)==WAIT_OBJECT_0,"parallel logging deadlocked");
        for(unsigned i=0;i<4;++i)CloseHandle(threads[i]);
        log=read(L"connection-trace.log");unsigned count=0;size_t pos=0;
        while((pos=log.find(" CONCURRENT ",pos))!=std::string::npos){++count;pos+=12;}
        check(count==400,"concurrent entries lost");
        check(GetFileAttributesW((root+L"\\WinXPlay-diagnostics.txt").c_str())==INVALID_FILE_ATTRIBUTES,"event logger created a collection report");
        std::cout<<"PASS: event history, rotation, concurrency, error preservation, newline sanitation\n";
        return 0;
#endif
    }catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}
}
