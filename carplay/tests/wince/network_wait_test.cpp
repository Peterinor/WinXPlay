#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "../../platform/common/network_wait.h"
static unsigned calls=0;static int mode=0;
static std::string lookup(){
    ++calls;
    if(mode==0)throw std::runtime_error("Waiting for receiver USB network IPv6 link-local address");
    if(mode==1)return "fe80::1234%3";
    if(mode==2)throw std::runtime_error("IPv6 interface enumeration failed");
    return "";
}
static void check(bool ok){if(!ok){puts("FAIL network wait");exit(1);}}
int main(){
    cp::CarPlayNetworkWait initial(50);
    check(!initial.timedOut(60049));check(initial.timedOut(60050));
    check(initial.poll(100,lookup).empty() && calls==0);
    initial.available();
    // Two simulated minutes without NCM: retain the session beyond the old
    // 60-second setup deadline; continue to return control to the iAP2 loop.
    for(DWORD t=100;t<120100;t+=100){check(initial.poll(t,lookup).empty());check(!initial.timedOut(t));}
    check(calls==120 && initial.waiting());
    mode=1;check(initial.poll(120100,lookup)=="fe80::1234%3");
    check(!initial.waiting() && !initial.timedOut(180099) && initial.timedOut(180100));
    check(initial.poll(121100,lookup).empty() && calls==121);
    // DiPlay answers every new availability notification. A new notification
    // must reannounce the endpoint without extending the media setup deadline.
    initial.available();check(initial.poll(121100,lookup)=="fe80::1234%3" && calls==122);
    check(initial.timedOut(180100));
    cp::CarPlayNetworkWait broken(0);broken.available();mode=2;bool thrown=false;
    try{broken.poll(0,lookup);}catch(const std::exception &){thrown=true;}check(thrown);
    cp::CarPlayNetworkWait empty(0);empty.available();mode=3;
    check(empty.poll(0,lookup).empty() && empty.waiting() && !empty.timedOut(120000));
    cp::CarPlayNetworkWait wrap(0xfffffff0UL);check(!wrap.timedOut(0x20));
    wrap.available();mode=0;wrap.poll(0xfffffff0UL,lookup);unsigned before=calls;
    wrap.poll(0x20,lookup);check(calls==before);wrap.poll(0x400,lookup);check(calls==before+1);
    puts("PASS network wait: delayed readiness, polling, deadlines, duplicate availability, hard errors, empty results, clock wrap");
}
