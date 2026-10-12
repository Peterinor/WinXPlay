#include "../../platform/common/receiver_context.h"
#include <iostream>
#include <cstdlib>
using namespace cp;
static unsigned lookups=0;
namespace cp {
// Only the test's lockdown.obj substitutes this symbol. Production lookup and
// the rest of the actual TLS/iAP2/Receiver stack remain unchanged.
std::string testReceiverAddresses(){
    ++lookups;
    if(environment(L"WINXPLAY_WAIT_TEST",L"")==L"closed" || lookups<3)
        throw std::runtime_error("Waiting for receiver USB network IPv6 link-local address");
    return "fe80::1234";
}
}
int main(int argc,char **argv){
    if(argc!=3)return 3;
    WSADATA data;WSAStartup(MAKEWORD(2,2),&data);
    try{
        UsbMux mux(unsigned(atoi(argv[2])));AppleDevice d;
        d.id=17;d.serial="SYNTHETIC";d.connection="USB";
        if(environment(L"WINXPLAY_WAIT_TEST",L"")==L"trust_only"){
            Value result=restorePhoneSession(mux,d);
            Bytes text=xmlPlist(result);std::cout<<std::string(text.begin(),text.end());return 0;
        }
        ReceiverContext context(0,0);
        Receiver receiver(context.services(),0,0,0,true);
        receiver.start();
        Value result=probeCarKit(mux,d,&receiver);
        receiver.stop();
        // Exercise the real result contract, including before availability.
        result.dict["test_waiting"]=Value::flag(result.at("waiting_for_network").boolean);
        result.dict["test_lookups"]=Value::num(lookups);
        result.dict["test_device_id"]=Value::str(receiver.deviceId());
        result.dict["test_serial"]=Value::str(receiver.serialNumber());
        Bytes text=xmlPlist(result);std::cout<<std::string(text.begin(),text.end());
        return result.dict.count("mfi_authentication_accepted")?0:2;
    }catch(const std::exception &e){std::cerr<<e.what();return 3;}
}
