#ifndef WINXPLAY_PHONE_UI_H
#define WINXPLAY_PHONE_UI_H
#include "native_services.h"
#include <memory>
namespace cp {
const UINT PhoneCommandMessage=WM_APP+6;
inline void postPhoneCommand(void* context,const Value& command) {
    if(!command.dict.count("type"))return;
    const std::string& type=command.at("type").string;
    if(type!="requestUI" && type!="modesChanged")return;
    Value* copy=new Value(command);
    if(!PostMessageW((HWND)context,PhoneCommandMessage,0,(LPARAM)copy))delete copy;
}
inline void applyPhoneCommand(HWND window,LPARAM parameter) {
    std::auto_ptr<Value> command((Value*)parameter);
    if(!command.get())return;
    const std::string& type=command->at("type").string;
    Value report=Value::dictionary();report.dict["type"]=Value::str(type);
    if(type=="requestUI") {
        std::string url;
        if(command->dict.count("params") && command->at("params").dict.count("url"))
            url=command->at("params").at("url").string;
        bool host=url!="videoplayback:";
        report.dict["host_ui_requested"]=Value::flag(host);
        // The phone's car/manufacturer button returns to the host. The OEM
        // launcher observes ordinary window visibility; no device protocol.
#ifdef UNDER_CE
        if(host)ShowWindow(window,SW_HIDE);
#else
        if(host)ShowWindow(window,SW_MINIMIZE);
#endif
    } else if(command->dict.count("params"))report.dict["modes"]=command->at("params");
    try{writeDiagnosticFile(executableDirectory()+L"/host-events.plist",xmlPlist(report));}catch(...){}
}
}
#endif
