#include "suite.h"
#include "../../devices/mrn/portable.h"
#include <sstream>
int WINAPI NetworkTestMain(HINSTANCE,HINSTANCE,LPWSTR,int);
int WINAPI ReceiverMain(HINSTANCE,HINSTANCE,LPWSTR,int);
using namespace cp;
namespace {
HANDLE logFile=INVALID_HANDLE_VALUE;
void stage(const char *s){
    std::ostringstream line;line<<GetTickCount()<<" "<<s<<"\r\n";std::string text=line.str();DWORD n=0;
    require(WriteFile(logFile,text.data(),DWORD(text.size()),&n,0)&&n==text.size(),"Connection log write failed");
    FlushFileBuffers(logFile);
}
int phone(const Value &config,bool pair) {
    Value report=Value::dictionary();WSADATA data;
    require(WSAStartup(MAKEWORD(2,2),&data)==0,"Winsock initialization failed");
    int code=1;
    try {
        stage("USB_PREPARE");Value usb=prepareUsbCarPlay();
        report.dict["usb"]=usb;
        require(!usb.at("mode_switch_requested").boolean,"USB re-enumeration requested; retry after reconnect");
        stage("PHONE_ENUMERATE");UsbMux mux;std::vector<AppleDevice> devices=mux.devices();
        report.dict["device_count"]=Value::num(devices.size());require(!devices.empty(),"No iPhone found");
        // Pairing records contain private keys. Never put them in a report.
        if(pair){stage("PHONE_PAIR_UNLOCK_AND_ACCEPT_TRUST");mux.pairRecord(devices[0]);report.dict["paired"]=Value::flag(true);}
        stage("PHONE_DIAGNOSTIC_COMPLETE");code=0;
    }catch(const std::exception &e){report.dict["error"]=Value::str(e.what());stage("PHONE_DIAGNOSTIC_FAILED");}
    report.dict["physical_carplay_verified"]=Value::flag(false);
    WSACleanup();writeFile(executableDirectory()+L"/phone-test.plist",xmlPlist(report));return code;
}
}
int WINAPI WinMain(HINSTANCE h,HINSTANCE prev,LPWSTR command,int show) {
    std::string mode,error;int code=1;
    HANDLE guard=CreateMutexW(0,FALSE,L"winxplay_test_connect.Singleton");
    if(!guard||GetLastError()==ERROR_ALREADY_EXISTS){if(guard)CloseHandle(guard);return 3;}
    try {
        Value config=suiteConfig();mode=suiteMode(command,config,"inspect");
        require(mode=="inspect"||mode=="--selftest"||mode=="usb-register"||mode=="phone"||mode=="pair"||mode=="session","Unknown connection mode");
        for(unsigned i=1;i<10000;++i){wchar_t name[64];swprintf(name,L"/connect-%04u.log",i);
            logFile=CreateFileW((executableDirectory()+name).c_str(),GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,0,0);
            if(logFile!=INVALID_HANDLE_VALUE)break;
            require(GetLastError()==ERROR_FILE_EXISTS||GetLastError()==ERROR_ALREADY_EXISTS,"Cannot create connection log");}
        require(logFile!=INVALID_HANDLE_VALUE,"Connection log slots exhausted");stage("START");
        Sleep(suiteNumber(config,"delay_seconds",0,120)*1000);
        if(mode=="inspect"||mode=="--selftest") {
            std::wstring argument=mode=="inspect"?L"":L"--selftest";
            stage("NETWORK_INSPECT");code=NetworkTestMain(h,prev,const_cast<LPWSTR>(argument.c_str()),show);
        }else{
            require(!FindWindowW(L"WinXPlay",0),"WinXPlay is already running; stop it before testing its USB connection");
            if(mode=="usb-register"){stage("USB_REGISTER");preparePortableReceiver();code=0;}
            else if(mode=="phone"||mode=="pair")code=phone(config,mode=="pair");
            else {stage("RECEIVER_SESSION_START");code=ReceiverMain(h,prev,L"",show);stage("RECEIVER_SESSION_EXIT");}
        }
    }catch(const std::exception &e){error=e.what();}
    try{suiteResult("connect",mode,code,error);}catch(...){code=1;}
    if(logFile!=INVALID_HANDLE_VALUE)CloseHandle(logFile);CloseHandle(guard);return code;
}
