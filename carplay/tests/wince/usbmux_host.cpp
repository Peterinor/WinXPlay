// Exercise the actual CE USBMUX adapter on Windows sockets with a synthetic USB phone.
#define CARPLAY_TEST_CE_USB
#define WINXPLAY_DIAGNOSTICS_TEST
#include "../../platform/common/native_services.h"
#include "../../platform/wince/usb_driver/contract.h"
#include <iostream>
using namespace cp;
namespace {
CRITICAL_SECTION phoneLock;
Bytes incoming;
bool attached=true;
unsigned generation=1,versions=0,setups=0,syns=0;
unsigned announcedPort=0,pairPort=0;
bool setupValid=true;
bool wireValid=true;
U16 phoneSequence=0;
U32 phoneTcpSequence=100;
volatile LONG clockOffset=0,ackPendingEvents=0,ackProgressEvents=0,rawResets=0,rawFins=0,rawBytes=0;
Bytes rawLast,rawPayload;
DWORD WINAPI muxClock(){return GetTickCount()+DWORD(InterlockedCompareExchange(&clockOffset,0,0));}
void queueTcp(const Bytes &tx,U8 flags,const Bytes &payload=Bytes()){
    Bytes b(36);put32(b,0,6);put32(b,4,unsigned(36+payload.size()));put32(b,8,0xfaceface);
    put16(b,12,phoneSequence++);put16(b,14,be16(tx,12));
    put16(b,16,be16(tx,18));put16(b,18,be16(tx,16));put32(b,20,phoneTcpSequence);
    put32(b,24,be32(tx,20)+unsigned(tx.size()-36)+((tx[29]&2)?1:0));b[28]=0x50;b[29]=flags;
    put16(b,30,512);b.insert(b.end(),payload.begin(),payload.end());
    incoming.insert(incoming.end(),b.begin(),b.end());phoneTcpSequence+=payload.size()+((flags&2)?1:0);
}
HANDLE WINAPI usbOpen(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE){
    if(!attached){SetLastError(ERROR_DEV_NOT_EXIST);return INVALID_HANDLE_VALUE;}
    return CreateEventW(0,TRUE,FALSE,0);
}
BOOL WINAPI usbQuery(HANDLE,DWORD,LPVOID,DWORD,LPVOID out,DWORD size,LPDWORD count,LPOVERLAPPED){
    if(!attached){SetLastError(ERROR_DEVICE_NOT_CONNECTED);return FALSE;}
    if(size!=sizeof(WinXPlayUsbInfo))return FALSE;
    WinXPlayUsbInfo v={sizeof(v),1,generation,1452,0x12a8,5,1,0x81,2,1};
    memcpy(out,&v,sizeof(v));*count=sizeof(v);return TRUE;
}
BOOL WINAPI usbWrite(HANDLE,LPCVOID input,DWORD size,LPDWORD count,LPOVERLAPPED){
    Bytes b((const U8*)input,(const U8*)input+size);EnterCriticalSection(&phoneLock);
    if(be32(b,0)==0){
        static const U8 expected[]={0,0,0,0,0,0,0,20,0,0,0,2,0,0,0,0,0,0,0,0};
        wireValid=wireValid && size==sizeof(expected) && !memcmp(input,expected,sizeof(expected));
        ++versions;incoming.insert(incoming.end(),b.begin(),b.end());
    }
    else if(be32(b,0)==2){
        // Literal usbmuxd v2 SETUP vector, independent of production encoders.
        static const U8 expected[]={0,0,0,2,0,0,0,17,0xfe,0xed,0xfa,0xce,0,0,0xff,0xff,7};
        ++setups;setupValid=setupValid && size==sizeof(expected) && !memcmp(input,expected,sizeof(expected));
    }
    else if(be32(b,0)==6){
        if(b[29]&2){
            if(be16(b,16)==1){
                static const U8 expected[]={0,0,0,6,0,0,0,36,0xfe,0xed,0xfa,0xce,0,1,0xff,0xff,
                    0,1,0xf2,0x7e,0,0,0,0,0,0,0,0,0x50,2,2,0,0,0,0,0};
                wireValid=wireValid && size==sizeof(expected) && !memcmp(input,expected,sizeof(expected));
            }
            ++syns;phoneTcpSequence=100;queueTcp(b,0x12);
        }
        else if(be16(b,18)==50000){
            if(b[29]&4)InterlockedIncrement(&rawResets);
            else if(b[29]&1){InterlockedIncrement(&rawFins);queueTcp(b,0x14);}
            else if(size>36){rawLast=b;rawPayload.insert(rawPayload.end(),b.begin()+36,b.end());InterlockedExchange(&rawBytes,LONG(rawPayload.size()));}
        }
        else if(size>36){
            Value request=parsePlist(slice(b,40,b.size()-40));
            Value reply=Value::dictionary();reply.dict["Request"]=request.at("Request");
            if(request.at("Request").string=="GetValue")reply.dict["Value"]=Value::str("00000000-0123456789ABCDEF");
            else if(request.at("Request").string=="SetValue")announcedPort=be16(b,16);
            else if(request.at("Request").string=="Pair"){
                pairPort=be16(b,16);
                if(!announcedPort || pairPort!=announcedPort)reply.dict["Error"]=Value::str("InvalidPairRecord");
            }
            Bytes xml=xmlPlist(reply),payload(4);put32(payload,0,unsigned(xml.size()));
            payload.insert(payload.end(),xml.begin(),xml.end());queueTcp(b,0x10,payload);
        }else if(b[29]&1)queueTcp(b,0x14);
    }
    LeaveCriticalSection(&phoneLock);*count=size;return TRUE;
}
BOOL WINAPI usbRead(HANDLE,LPVOID out,DWORD size,LPDWORD count,LPOVERLAPPED){
    for(unsigned i=0;i<25;++i){
        EnterCriticalSection(&phoneLock);
        if(!attached){LeaveCriticalSection(&phoneLock);SetLastError(ERROR_DEVICE_NOT_CONNECTED);return FALSE;}
        if(!incoming.empty()){
            // Split mux/TCP headers across reads to exercise buffering too.
            unsigned n=unsigned(incoming.size());if(n>7)n=7;if(n>size)n=size;
            memcpy(out,&incoming[0],n);incoming.erase(incoming.begin(),incoming.begin()+n);
            LeaveCriticalSection(&phoneLock);*count=n;return TRUE;
        }
        LeaveCriticalSection(&phoneLock);Sleep(1);
    }
    SetLastError(ERROR_TIMEOUT);return FALSE;
}
}
#define CreateFileW usbOpen
#define DeviceIoControl usbQuery
#define ReadFile usbRead
#define WriteFile usbWrite
#define GetTickCount muxClock
#include "../../platform/wince/usbmux.cpp"
#undef GetTickCount
#undef CreateFileW
#undef DeviceIoControl
#undef ReadFile
#undef WriteFile
namespace cp {
void diagnosticEvent(const char *event,const std::string&){
    if(!strcmp(event,"MUX_ACK_PENDING"))InterlockedIncrement(&ackPendingEvents);
    if(!strcmp(event,"MUX_ACK_PROGRESS"))InterlockedIncrement(&ackProgressEvents);
}
std::wstring executableDirectory(){return L".";}
std::wstring environment(const wchar_t*,const wchar_t* fallback){return fallback;}
std::string utf8(const std::wstring &s){return std::string(s.begin(),s.end());}
void writeFile(const std::wstring&,const Bytes&){}
Value ensurePhonePair(PhonePairChannel& channel,const std::wstring&,const std::string&){
    Value set=Value::dictionary();set.dict["Request"]=Value::str("SetValue");
    set.dict["Key"]=Value::str("UntrustedHostBUID");set.dict["Value"]=Value::str("synthetic-buid");
    require(channel.exchange(set).at("Request").string=="SetValue","BUID registration reply missing");
    Value pair=Value::dictionary();pair.dict["Request"]=Value::str("Pair");
    Value record=Value::dictionary();
    record.dict["RootCertificate"]=Value::blob(bytes("synthetic-root"));
    record.dict["HostCertificate"]=Value::blob(bytes("synthetic-host"));
    record.dict["DeviceCertificate"]=Value::blob(bytes("synthetic-device"));
    pair.dict["PairRecord"]=record;
    Value result=channel.exchange(pair);require(!result.dict.count("Error"),"Pair changed Lockdown channel");return result;
}
}
namespace {
void waitCounter(volatile LONG &counter,LONG expected,const char *message){
    DWORD start=GetTickCount();
    while(InterlockedCompareExchange(&counter,0,0)<expected && GetTickCount()-start<3000)Sleep(5);
    require(InterlockedCompareExchange(&counter,0,0)==expected,message);
}
void delayedAckTest(){
    UsbMux mux;AppleDevice device;device.id=generation;
    SOCKET s=mux.connectDevice(device,50000);
    Bytes data(38,0x38);sendAll(s,data);waitCounter(rawBytes,38,"raw tail not delivered");
    // Advance only the production transport clock: reproduce the vehicle's
    // 38-byte and 152-byte outstanding tails without sleeping for minutes.
    InterlockedExchangeAdd(&clockOffset,12000);
    waitCounter(ackPendingEvents,1,"38-byte delayed ACK did not reach diagnostic threshold");
    data.assign(114,0x52);sendAll(s,data);waitCounter(rawBytes,152,"quiet CarKit channel was closed after 10s");
    InterlockedExchangeAdd(&clockOffset,60000);Sleep(100);
    require(rawResets==0 && ackPendingEvents==1,"pending ACK caused reset or repetitive diagnostics");
    // Leave the entire 16 KiB window unacknowledged, then make sure additional
    // bytes stay queued locally and are delivered once the phone acknowledges.
    data.assign(16484-152,0x71);sendAll(s,data);waitCounter(rawBytes,16384,"send window not filled");
    InterlockedExchangeAdd(&clockOffset,60000);Sleep(100);
    require(rawBytes==16384 && rawResets==0,"send window exceeded or stalled channel reset");
    EnterCriticalSection(&phoneLock);queueTcp(rawLast,0x10,bytes("ok"));LeaveCriticalSection(&phoneLock);
    require(receive(s,2)==bytes("ok"),"late ACK response did not reach local client");
    waitCounter(rawBytes,16484,"late ACK did not resume queued transmission");
    waitCounter(ackProgressEvents,1,"late ACK progress missing");
    Bytes expected(38,0x38);expected.insert(expected.end(),114,0x52);expected.insert(expected.end(),16484-152,0x71);
    EnterCriticalSection(&phoneLock);
    bool exact=rawPayload==expected;
    queueTcp(rawLast,0x10);LeaveCriticalSection(&phoneLock);
    require(exact,"delayed ACK duplicated or lost bytes");
    closesocket(s);waitCounter(rawFins,1,"local cancellation did not close channel");
    // A real peer reset must still terminate the client socket.
    EnterCriticalSection(&phoneLock);rawPayload.clear();InterlockedExchange(&rawBytes,0);LeaveCriticalSection(&phoneLock);
    s=mux.connectDevice(device,50000);sendAll(s,Bytes(38,0x38));waitCounter(rawBytes,38,"reset fixture not sent");
    EnterCriticalSection(&phoneLock);queueTcp(rawLast,0x14);LeaveCriticalSection(&phoneLock);
    char byte;require(recv(s,&byte,1,0)<=0,"peer reset was ignored");closesocket(s);
    std::cout<<"PASS: delayed ACK 38/152 bytes, 132s quiet connection, 16 KiB backpressure, late ACK resumes exact bytes, local close, peer reset\n";
}
}
int main(){
    InitializeCriticalSection(&phoneLock);
    try{
        for(unsigned i=0;i<12;++i){
            require(!prepareUsbCarPlay().at("mode_switch_requested").boolean,"unexpected mode change");
            {UsbMux mux;require(mux.devices()[0].serial=="00000000-0123456789ABCDEF","reply parsing failed");}
            Sleep(60);
        }
        require(versions==1 && setups==1 && syns==12,"retry renegotiated USB or exhausted sockets");
        require(setupValid,"SETUP sequence/ack incorrect");
        require(wireValid,"VERSION/SETUP/first SYN wire bytes differ from reference vectors");
        {UsbMux mux;AppleDevice device;device.id=generation;device.serial="00000000-0123456789ABCDEF";
            mux.pairRecord(device);require(announcedPort && pairPort==announcedPort,"BUID and Pair used different connections");}
        delayedAckTest();
        EnterCriticalSection(&phoneLock);attached=false;LeaveCriticalSection(&phoneLock);
        bool failed=false;try{prepareUsbCarPlay();}catch(...){failed=true;}require(failed,"detach not detected");
        EnterCriticalSection(&phoneLock);incoming.clear();++generation;attached=true;phoneSequence=0;LeaveCriticalSection(&phoneLock);
        {UsbMux mux;require(mux.devices().size()==1,"reattach failed");}
        require(versions==2 && setups==2 && setupValid && wireValid,"new attachment did not reinitialize transport");
        EnterCriticalSection(&phoneLock);attached=false;LeaveCriticalSection(&phoneLock);
        try{prepareUsbCarPlay();}catch(...){}
        std::cout<<"PASS: initial SETUP, phone RX marker, fragmented replies, 12 cached retries, socket cleanup, BUID/Pair channel reuse, detach/reattach\n";
        DeleteCriticalSection(&phoneLock);return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}
}
