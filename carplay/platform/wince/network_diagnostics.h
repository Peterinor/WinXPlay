// Read-only, captured while the phone is attached. Kept in connection-trace so
// unplugging to collect logs cannot replace the useful state with an empty one.
#if defined(UNDER_CE) && WINXPLAY_ENABLE_LOGS
// Read-only CE NCM registry and driver checks.
static void recordNcmNetworkState(){
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

static std::vector<std::wstring> networkNdisList(HANDLE h,DWORD code,
        const std::wstring &adapter,const char *event) {
    WCHAR data[8192]={0};DWORD size=0;
    BOOL ok=DeviceIoControl(h,code,adapter.empty()?0:(void*)adapter.c_str(),
        adapter.empty()?0:DWORD((adapter.size()+1)*sizeof(WCHAR)),data,sizeof(data),&size,0);
    DWORD error=ok?0:GetLastError();std::vector<std::wstring> names;
    std::ostringstream detail;detail<<"adapter="<<utf8(adapter)<<" ok="<<ok<<" error="<<error<<" bytes="<<size;
    if(ok && size<=sizeof(data) && size%sizeof(WCHAR)==0){
        size_t count=size/sizeof(WCHAR),at=0;
        while(at<count && data[at] && names.size()<64){
            size_t end=at;while(end<count && data[end])++end;
            if(end==count){detail<<" malformed=1";break;}
            names.push_back(std::wstring(data+at,end-at));at=end+1;
        }
    }
    for(size_t i=0;i<names.size();++i)detail<<" item="<<utf8(names[i]);
    diagnosticEvent(event,detail.str());return names;
}
static void recordNetworkState(unsigned generation){
    static unsigned previous=0;static DWORD tick=0;
    if(!generation)generation=previous;
    DWORD now=GetTickCount();if(previous==generation && DWORD(now-tick)<30000)return;
    try{
        HANDLE h=CreateFileW(DD_NDIS_DEVICE_NAME,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
            0,OPEN_EXISTING,0,0);
        DWORD error=h==INVALID_HANDLE_VALUE?GetLastError():0;
        std::ostringstream detail;detail<<"generation="<<generation<<" open_error="<<error;
        diagnosticEvent("NETWORK_SNAPSHOT",detail.str());
        if(h!=INVALID_HANDLE_VALUE){
            try{
                networkNdisList(h,IOCTL_NDIS_GET_PROTOCOL_NAMES,L"","NETWORK_NDIS_PROTOCOLS");
                std::vector<std::wstring> names=networkNdisList(h,IOCTL_NDIS_GET_ADAPTER_NAMES,L"","NETWORK_NDIS_ADAPTERS");
                for(size_t i=0;i<names.size();++i)
                    networkNdisList(h,IOCTL_NDIS_GET_ADAPTER_BINDINGS,names[i],"NETWORK_NDIS_BINDINGS");
            }catch(...){CloseHandle(h);throw;}
            CloseHandle(h);
        }
        recordNcmNetworkState();
        previous=generation;tick=now;
    }catch(...){diagnosticEvent("NETWORK_SNAPSHOT_FAILED");}
}
#else
static void recordNetworkState(unsigned){}
#endif
