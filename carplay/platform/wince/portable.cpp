#include "portable.h"
#include "../common/native_crypto.h"
#include "../common/local_identity.h"
#include "usb_driver/contract.h"
#include "usb_configuration.h"
#include "usb_loader_probe.h"
#include <sstream>
namespace cp {
void prepareWinceConnection(void (*status)(void *,const std::string &)) {
    static bool ready=false;
    if(!ready && environment(L"CARPLAY_PORTABLE",L"0")==L"1") {
        if(status)status(0,"Preparing USB test; connect iPhone after startup");
        preparePortableReceiver();
        ready=true;
    }
}

void preparePortableReceiver() {
    const std::wstring folder=executableDirectory();
    Value report=Value::dictionary();
    report.dict["mode"]=Value::str("usb_portable");
    report.dict["physical_carplay_verified"]=Value::flag(false);
    try {
        // Validate before registering anything. Paths are relative to this EXE.
        LocalAuth identity(folder+L"/identity",L"static wolfCrypt");
        report.dict["identity_validated"]=Value::flag(true);
        const std::wstring encrypted=folder+L"/phone-pair.dat";
        const std::wstring plainPath=folder+L"\\phone-pair.plist";
        Bytes plain,stored;
        try{stored=protectedRead(encrypted);}catch(const std::exception &){}
        if(GetFileAttributesW(plainPath.c_str())!=INVALID_FILE_ATTRIBUTES)
            plain=readFile(plainPath,1024*1024);
        else plain=stored;
        if(!plain.empty()){
        Value pair=parsePlist(plain);
        require(!pair.at("DeviceUDID").string.empty() && !pair.at("HostID").string.empty() &&
                !pair.at("SystemBUID").string.empty() && !pair.at("RootPrivateKey").data.empty() &&
                !pair.at("RootCertificate").data.empty() && !pair.at("DeviceCertificate").data.empty(),
                "Incomplete trusted iPhone pairing record");
        if(stored!=plain)protectedWrite(encrypted,plain);
        require(protectedRead(encrypted)==plain,"Portable pair store readback failed");
        SecureZeroMemory(&plain[0],plain.size());
        if(!stored.empty())SecureZeroMemory(&stored[0],stored.size());
        report.dict["pair_store_ready"]=Value::flag(true);
        }else{
            report.dict["pairing_on_connection"]=Value::flag(true);
        }
        std::wstring dllPath=folder+L"\\WinXPlayUsbMux.dll";
        // Keep a fresh, collectable trace even if the USB host never calls the DLL.
        // Failure to write diagnostics must not prevent USB registration.
        try {
            std::ostringstream header;
            header << "WinXPlay USB trace; columns: uptime_hex stage a_hex b_hex c_hex\r\n"
                   << "RECEIVER_START uptime_ms=" << GetTickCount() << " pid=" << GetCurrentProcessId()
                   << "; attach callbacks follow if trace is writable\r\n";
            std::string text=header.str();
            writeDiagnosticFile(folder+L"/usb-driver.log",Bytes(text.begin(),text.end()));
        } catch (...) {}
        report.dict["usb_driver_path"]=Value::str(utf8(dllPath));
        SetLastError(0);
        HMODULE dll=LoadLibraryW(dllPath.c_str());
        report.dict["usb_driver_load_error"]=Value::num(dll?0:GetLastError());
        require(dll!=0,"USB test package cannot load WinXPlayUsbMux.dll");
        typedef BOOL (*Install)(LPCWSTR);
        Install install=(Install)GetProcAddress(dll,L"USBInstallDriver");
        SetLastError(0);
        BOOL ok=install?install(dllPath.c_str()):FALSE;
        DWORD error=install?GetLastError():ERROR_PROC_NOT_FOUND;
        WinXPlayUsbRegistration detail={0};
        typedef BOOL (*Status)(WinXPlayUsbRegistration*,DWORD);
        Status getStatus=(Status)GetProcAddress(dll,L"USBGetRegistrationStatus");
        BOOL details=getStatus&&getStatus(&detail,sizeof(detail));
        report.dict["usb_registration_error"]=Value::num(ok?0:error);
        if(details){
            report.dict["usb_registration_stage"]=Value::num(detail.stage);
            report.dict["usb_registration_error"]=Value::num(detail.error);
            report.dict["usb_registration_registry_fallback"]=Value::flag(detail.fallback!=0);
            report.dict["usb_system_id_result"]=Value::num(detail.idResult);
            report.dict["usb_system_id_error"]=Value::num(detail.idError);
            report.dict["usb_system_settings_result"]=Value::num(detail.settingsResult);
            report.dict["usb_system_settings_error"]=Value::num(detail.settingsError);
        }
        FreeLibrary(dll);
        if(!ok){
            std::ostringstream message;message<<"iPhone USB client registration failed: stage="
                <<(details?detail.stage:0)<<", error="<<(details?detail.error:error);
            throw std::runtime_error(message.str());
        }
        // No NAND copy and no registry flush. USB matching requires registration;
        // the USB host loads this DLL from the executable directory on enumeration.
        report.dict["usb_client_registered"]=Value::flag(true);
        // Clear only our diagnostic subkey so old attach events cannot look new.
        // Record failures, but never let optional tracing block connection.
        LONG reset=RegDeleteKeyW(HKEY_LOCAL_MACHINE,WINXPLAY_USB_DIAGNOSTICS_KEY);
        report.dict["usb_trace_reset_error"]=Value::num(reset==ERROR_FILE_NOT_FOUND?0:reset);
        HKEY traceKey=0;DWORD disposition=0;
        LONG traceError=RegCreateKeyExW(HKEY_LOCAL_MACHINE,WINXPLAY_USB_DIAGNOSTICS_KEY,
            0,0,0,0,0,&traceKey,&disposition);
        if(traceError==ERROR_SUCCESS){
            DWORD now=GetTickCount();
            traceError=RegSetValueExW(traceKey,L"ReceiverStartUptime",0,REG_DWORD,(const BYTE*)&now,sizeof(now));
            RegCloseKey(traceKey);
        }
        report.dict["usb_trace_registry_error"]=Value::num(traceError);
        report.dict["usb_diagnostics_version"]=Value::num(5);
        report.dict["usb_stream_activation"]=Value::str("ActivateDeviceEx");
        // LoadLibrary success above is not evidence that USBD's LoadDriver works.
        // This uses the same loader API from the receiver process; USB host context
        // and actual device matching still require the subsequent attach trace.
        UsbLoaderProbe loader=probeUsbDriverLoader(dllPath.c_str());
        Value loading=Value::dictionary();
        loading.dict["api_available"]=Value::flag(loader.available);
        loading.dict["loaded"]=Value::flag(loader.loaded);
        loading.dict["load_error"]=Value::num(loader.error);
        loading.dict["attach_export"]=Value::flag(loader.attachExport);
        loading.dict["export_error"]=Value::num(loader.exportError);
        loading.dict["release_error"]=Value::num(loader.releaseError);
        loading.dict["called_from_receiver_process"]=Value::flag(true);
        report.dict["usb_driver_loader_probe"]=loading;
        AppleUsbConfiguration config;
        prepareAppleUsbConfiguration(config);
        Value selection=Value::dictionary();
        selection.dict["apple_vendor_id"]=Value::num(1452);
        selection.dict["strategy"]=Value::str("descriptor_bootstrap");
        selection.dict["previous_known"]=Value::flag(config.previousKnown);
        if(config.previousKnown)selection.dict["previous"]=Value::num(config.previous);
        selection.dict["readback"]=Value::num(config.readback);
        selection.dict["changed"]=Value::flag(config.changed);
        selection.dict["stage"]=Value::str(config.stage);
        selection.dict["error"]=Value::num(config.error);
        selection.dict["rollback_error"]=Value::num(config.rollbackError);
        selection.dict["requires_iphone_reenumeration"]=Value::flag(true);
        report.dict["apple_usb_configuration"]=selection;
        if(config.error){
            std::ostringstream message;message<<"Apple USB configuration failed: stage="
                <<config.stage<<", error="<<config.error;
            throw std::runtime_error(message.str());
        }
        AppleNcmBinding ncm;prepareAppleNcmBinding(ncm);
        Value binding=Value::dictionary();
        binding.dict["stage"]=Value::str(ncm.stage);
        binding.dict["error"]=Value::num(ncm.error);
        binding.dict["rollback_error"]=Value::num(ncm.rollbackError);
        binding.dict["changed"]=Value::flag(ncm.changed);
        binding.dict["vendor_id"]=Value::num(1452);
        binding.dict["device_class_subclass_protocol"]=Value::str("0_0_0");
        binding.dict["registry_path"]=Value::str("Drivers\\USB\\LoadClients\\1452\\0_0_0\\2_13_0\\USBCDCNCM");
        binding.dict["interface_class_subclass_protocol"]=Value::str("2_13_0");
        binding.dict["dll"]=Value::str("usbcdcncm.dll");
        report.dict["apple_ncm_binding"]=binding;
        if(ncm.error){std::ostringstream message;message<<"Apple NCM driver registration failed: "
            <<ncm.stage<<" error="<<ncm.error;throw std::runtime_error(message.str());}
        // Loading is diagnostic only: success does not prove USB attachment or
        // miniport initialization. Do not invoke USBDeviceAttach ourselves.
        UsbLoaderProbe ncmLoader=probeUsbDriverLoader(L"\\Windows\\usbcdcncm.dll");
        Value ncmLoading=Value::dictionary();
        ncmLoading.dict["api_available"]=Value::flag(ncmLoader.available);
        ncmLoading.dict["loaded"]=Value::flag(ncmLoader.loaded);
        ncmLoading.dict["load_error"]=Value::num(ncmLoader.error);
        ncmLoading.dict["attach_export"]=Value::flag(ncmLoader.attachExport);
        ncmLoading.dict["export_error"]=Value::num(ncmLoader.exportError);
        ncmLoading.dict["release_error"]=Value::num(ncmLoader.releaseError);
        report.dict["ncm_driver_loader_probe"]=ncmLoading;
        report.dict["ready"]=Value::flag(true);
        writeDiagnosticFile(folder+L"/portable-startup.plist",xmlPlist(report));
    }catch(const std::exception &e){
        report.dict["ready"]=Value::flag(false);report.dict["error"]=Value::str(e.what());
        try{writeDiagnosticFile(folder+L"/portable-startup.plist",xmlPlist(report));}catch(...){}
        throw;
    }
}
}
