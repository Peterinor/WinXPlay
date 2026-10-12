// Read-only F3.21 NCM registry and driver checks, included inside the CE adapter.
static void recordDeviceNetworkState(){
        struct Field{const wchar_t *path,*name;};
        const Field fields[]={
            {L"Comm\\USBCDCNCM",L"ImagePath"},
            {L"Comm\\USBCDCNCM1",L"ImagePath"},
            {L"Comm\\USBCDCNCM1\\Parms\\Tcpip6",L"Enabled"},
            {L"Comm\\Tcpip6\\Parms",L"DisabledComponents"},
            {L"Drivers\\USB\\LoadClients\\65535_65535\\0_0_0\\2_13_0\\USBCDCNCM",L"DLL"},
            {L"Drivers\\USB\\LoadClients\\Default\\Default\\2_13_0\\USBCDCNCM",L"DLL"},
            {L"Drivers\\USB\\LoadClients\\1452\\Default\\2_13_0\\USBCDCNCM",L"DLL"},
            {L"Drivers\\USB\\LoadClients\\1452\\0_0_0\\2_13_0\\USBCDCNCM",L"DLL"}
        };
        for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);++i){
            Value v=usbRegistryValue(fields[i].path,fields[i].name);Bytes encoded=xmlPlist(v);
            diagnosticEvent("NETWORK_REGISTRY",utf8(fields[i].path)+" "+utf8(fields[i].name)+" "+std::string(encoded.begin(),encoded.end()));
        }
        DWORD attributes=GetFileAttributesW(L"\\Windows\\usbcdcncm.dll");
        std::ostringstream dll;dll<<"present="<<(attributes!=INVALID_FILE_ATTRIBUTES)<<" error="
            <<(attributes==INVALID_FILE_ATTRIBUTES?GetLastError():0);
        diagnosticEvent("NETWORK_NCM_DLL",dll.str());
}
