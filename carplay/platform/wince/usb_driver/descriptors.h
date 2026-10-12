// CE7 appends fields to the WM5 USB structures. Advance by the host's dwCount,
// never by the SDK's sizeof. Only the common prefix is accessed here.
template<class T> static const T* usbNext(const T* item) {
    if (!item || item->dwCount < sizeof(T) || item->dwCount > 4096 ||
        (item->dwCount & 3)) return 0;
    return (const T*)((const BYTE*)item + item->dwCount);
}
enum { USB_HAS_MUX=1, USB_HAS_NCM=2, USB_HAS_PTP=4 };
static DWORD configurationFeatures(LPCUSB_CONFIGURATION config) {
    if (!usbNext(config) || config->dwNumInterfaces > 256) return 0;
    DWORD features=0;
    LPCUSB_INTERFACE item=config->lpInterfaces;
    for (DWORD i=0;i<config->dwNumInterfaces;++i) {
        LPCUSB_INTERFACE next=usbNext(item);
        if (!next) return 0;
        const USB_INTERFACE_DESCRIPTOR& v=item->Descriptor;
        if (v.bInterfaceClass==255 && v.bInterfaceSubClass==254 && v.bInterfaceProtocol==2)
            features|=USB_HAS_MUX;
        if (v.bInterfaceClass==2 && v.bInterfaceSubClass==13) features|=USB_HAS_NCM;
        if (v.bInterfaceClass==6 && v.bInterfaceSubClass==1 && v.bInterfaceProtocol==1)
            features|=USB_HAS_PTP;
        item=next;
    }
    return features;
}
static DWORD carPlayConfiguration(LPCUSB_DEVICE desc,DWORD& offeredFeatures) {
    offeredFeatures=0;
    DWORD selected=0;
    LPCUSB_CONFIGURATION config=desc->lpConfigs;
    for (DWORD i=0;i<desc->Descriptor.bNumConfigurations;++i) {
        LPCUSB_CONFIGURATION next=usbNext(config);
        if (!next) { usbTrace("CONFIG_INVALID index",i);offeredFeatures=0;return 0; }
        DWORD features=configurationFeatures(config);
        offeredFeatures|=features;
        usbTrace("CONFIG value features interfaces",config->Descriptor.bConfigurationValue,
            features,config->dwNumInterfaces);
        if(features & USB_HAS_NCM){
            LPCUSB_INTERFACE item=config->lpInterfaces;
            for(DWORD j=0;j<config->dwNumInterfaces;++j){
                LPCUSB_INTERFACE after=usbNext(item);if(!after)break;
                const USB_INTERFACE_DESCRIPTOR &v=item->Descriptor;
                if(v.bInterfaceClass==2 && v.bInterfaceSubClass==13)
                    usbTrace("NCM_DESCRIPTOR config interface class_subclass_protocol",
                        config->Descriptor.bConfigurationValue,v.bInterfaceNumber,
                        (DWORD(v.bInterfaceClass)<<16)|(DWORD(v.bInterfaceSubClass)<<8)|v.bInterfaceProtocol);
                item=after;
            }
        }
        if (!selected && (features & 3)==3) selected=config->Descriptor.bConfigurationValue;
        config=next;
    }
    return selected;
}
