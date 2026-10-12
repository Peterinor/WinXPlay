// Apple bootstrap plus ff/fe/02 stream; ready configurations leave NCM to the OS.
#include "contract.h"
#include <usbdi.h>
extern "C" HANDLE WINAPI ActivateDeviceEx(LPCWSTR, LPCVOID, DWORD, LPVOID);
extern "C" BOOL WINAPI DeactivateDevice(HANDLE);
static HMODULE module;
static LONG generation;
#include "trace.h"
#include "descriptors.h"
struct Device {
    USB_HANDLE usb;
    LPCUSB_FUNCS api;
    USB_PIPE in, out;
    HANDLE registered;
    volatile LONG refs, gone, opened;
    CRITICAL_SECTION readLock, writeLock;
    WinXPlayUsbInfo info;
    DWORD targetConfiguration;
    BOOL bootstrap, transitionRequested;
    HANDLE closedForTransition;
    HMODULE workerModule;
    // The host polls ReadFile; an idle poll must not cancel a USB transaction.
    USB_TRANSFER receiveTransfer;
    HANDLE receiveEvent;
    BYTE *receiveBuffer;
    DWORD receiveSize, receiveOffset, receiveLogs, writeLogs;
};
// Only a synchronous activation in this same driver instance may supply a pointer.
// Flags=0 keeps the stream driver in the USB host's kernel/device-manager space.
static LONG pendingInit;
static const WCHAR streamKey[]=L"Drivers\\WinXPlay\\UsbMuxStream";
static void retain(Device *d) {
    InterlockedIncrement((LONG *)&d->refs);
}
static void release(Device *d) {
    if (InterlockedDecrement((LONG *)&d->refs) == 0) {
        DeleteCriticalSection(&d->readLock);
        DeleteCriticalSection(&d->writeLock);
        if (d->closedForTransition) CloseHandle(d->closedForTransition);
        LocalFree(d);
    }
}
static DWORD WINAPI complete(LPVOID event) {
    SetEvent((HANDLE)event);
    return 0;
}
// Caller holds readLock. The buffer/event outlive every asynchronous transfer.
static void closeReceive(Device *d) {
    if (d->receiveTransfer) {
        d->api->lpAbortTransfer(d->receiveTransfer, 0);
        d->api->lpCloseTransfer(d->receiveTransfer);
        d->receiveTransfer = 0;
    }
    if (d->receiveEvent) CloseHandle(d->receiveEvent);
    if (d->receiveBuffer) LocalFree(d->receiveBuffer);
    d->receiveEvent = 0; d->receiveBuffer = 0;
    d->receiveSize = d->receiveOffset = 0;
}
static DWORD readBulk(Device *d, void *buffer, DWORD size) {
    if (!size) return 0;
    if (size > 65536) { SetLastError(ERROR_INVALID_PARAMETER); return DWORD(-1); }
    EnterCriticalSection(&d->readLock);
    DWORD result = DWORD(-1), error = ERROR_DEVICE_NOT_CONNECTED;
    if (!d->gone && d->in) {
        if (!d->receiveBuffer) d->receiveBuffer = (BYTE*)LocalAlloc(LPTR,65536);
        if (!d->receiveEvent) d->receiveEvent = CreateEventW(0,TRUE,FALSE,0);
        if (!d->receiveBuffer || !d->receiveEvent) error = ERROR_NOT_ENOUGH_MEMORY;
        else {
            if (!d->receiveTransfer && d->receiveOffset == d->receiveSize) {
                d->receiveSize = d->receiveOffset = 0;
                ResetEvent(d->receiveEvent);
                d->receiveTransfer = d->api->lpIssueBulkTransfer(d->in,complete,d->receiveEvent,
                    USB_NO_WAIT|USB_IN_TRANSFER|USB_SHORT_TRANSFER_OK,65536,d->receiveBuffer,0);
                if (!d->receiveTransfer) error = GetLastError();
            }
            if (d->receiveTransfer) {
                DWORD wait = WaitForSingleObject(d->receiveEvent,25);
                if (wait == WAIT_TIMEOUT) error = ERROR_TIMEOUT; // keep request pending
                else if (wait != WAIT_OBJECT_0) { error = GetLastError(); closeReceive(d); }
                else {
                    DWORD actual=0, usbError=0;
                    BOOL ok=d->api->lpGetTransferStatus(d->receiveTransfer,&actual,&usbError);
                    d->api->lpCloseTransfer(d->receiveTransfer);d->receiveTransfer=0;
                    if(d->receiveLogs++<12) usbTrace("BULK_RX status bytes usb_error",ok,actual,usbError);
                    if (ok && !usbError && !d->gone && actual<=65536) {
                        d->receiveSize=actual; error=0; result=0;
                    } else error=ERROR_DEVICE_NOT_CONNECTED;
                }
            }
            if (!d->gone && d->receiveOffset < d->receiveSize) {
                result=d->receiveSize-d->receiveOffset;if(result>size)result=size;
                memcpy(buffer,d->receiveBuffer+d->receiveOffset,result);
                d->receiveOffset+=result;error=0;
            }
        }
    }
    LeaveCriticalSection(&d->readLock);
    if(error){SetLastError(error);return DWORD(-1);}return result;
}
static BOOL finish(Device *d, USB_TRANSFER transfer, HANDLE event, DWORD timeout, DWORD *count) {
    if (!transfer)
        return FALSE;
    BOOL waited = WaitForSingleObject(event, timeout) == WAIT_OBJECT_0;
    if (!waited)
        d->api->lpAbortTransfer(transfer, 0); // blocking abort before freeing caller memory
    DWORD usbError = 0, actual = 0;
    BOOL ok = d->api->lpGetTransferStatus(transfer, &actual, &usbError);
    d->api->lpCloseTransfer(transfer);
    if (!waited) {
        SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    if (!ok || usbError != USB_NO_ERROR || d->gone) {
        SetLastError(ERROR_DEVICE_NOT_CONNECTED);
        return FALSE;
    }
    *count = actual;
    return TRUE;
}
static DWORD transfer(Device *d, BOOL input, void *buffer, DWORD size) {
    if (!size)
        return 0;
    if (size > 65536) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return DWORD(-1);
    }
    CRITICAL_SECTION *lock = input ? &d->readLock : &d->writeLock;
    EnterCriticalSection(lock);
    DWORD count = 0;
    BOOL ok = FALSE;
    if (!d->gone && d->in && d->out) {
        HANDLE event = CreateEventW(0, TRUE, FALSE, 0);
        if (event) {
            DWORD flags =
                USB_NO_WAIT | (input ? USB_IN_TRANSFER | USB_SHORT_TRANSFER_OK : USB_OUT_TRANSFER);
            USB_TRANSFER t = d->api->lpIssueBulkTransfer(input ? d->in : d->out, complete, event,
                                                         flags, size, buffer, 0);
            ok = finish(d, t, event, input ? 25 : 2000, &count);
            if (!input && d->writeLogs++<12)
                usbTrace("BULK_TX requested actual error",size,count,ok?0:GetLastError());
            CloseHandle(event);
        }
    } else
        SetLastError(ERROR_DEVICE_NOT_CONNECTED);
    LeaveCriticalSection(lock);
    return ok ? count : DWORD(-1);
}
static BOOL WINAPI notify(LPVOID context, DWORD code, LPDWORD *, LPDWORD *, LPDWORD *, LPDWORD *) {
    if (code != USB_CLOSE_DEVICE)
        return FALSE;
    Device *d = (Device *)context;
    usbTrace("DETACH generation", d->info.generation);
    InterlockedExchange((LONG *)&d->gone, 1);
    if (d->in) d->api->lpAbortPipeTransfers(d->in, 0);
    if (d->out) d->api->lpAbortPipeTransfers(d->out, 0);
    EnterCriticalSection(&d->readLock);
    EnterCriticalSection(&d->writeLock);
    closeReceive(d);
    if (d->in) d->api->lpClosePipe(d->in);
    if (d->out) d->api->lpClosePipe(d->out);
    d->in = d->out = 0;
    LeaveCriticalSection(&d->writeLock);
    LeaveCriticalSection(&d->readLock);
    if (d->registered)
        DeactivateDevice(d->registered);
    release(d);
    return TRUE;
}
extern "C" DWORD CMX_Init(LPCWSTR activeKey, DWORD busContext) {
    usbTrace("CMX_INIT pid context", GetCurrentProcessId(), busContext);
    if (!activeKey || !busContext || DWORD(InterlockedCompareExchange(&pendingInit,0,0))!=busContext) {
        usbTrace("CMX_INIT_REJECT_CONTEXT",busContext);
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    Device *d = (Device *)busContext;
    retain(d);
    return busContext;
}
extern "C" BOOL CMX_Deinit(DWORD context) {
    release((Device *)context);
    return TRUE;
}
static HANDLE activateStream(Device *d, const WCHAR *path) {
    if (InterlockedCompareExchange(&pendingInit,(LONG)d,0)!=0) {
        SetLastError(ERROR_BUSY);
        return 0;
    }
    HKEY key=0;DWORD disposition=0;
    LONG error=RegCreateKeyExW(HKEY_LOCAL_MACHINE,streamKey,0,0,0,KEY_WRITE,0,&key,&disposition);
    if (error==ERROR_SUCCESS) {
        error=RegSetValueExW(key,L"DLL",0,REG_SZ,(const BYTE*)path,(lstrlenW(path)+1)*sizeof(WCHAR));
        const WCHAR prefix[]=L"CMX";DWORD index=1,flags=0;
        if (!error) error=RegSetValueExW(key,L"Prefix",0,REG_SZ,(const BYTE*)prefix,sizeof(prefix));
        if (!error) error=RegSetValueExW(key,L"Index",0,REG_DWORD,(const BYTE*)&index,sizeof(index));
        if (!error) error=RegSetValueExW(key,L"Flags",0,REG_DWORD,(const BYTE*)&flags,sizeof(flags));
        RegCloseKey(key);
    }
    usbTrace("STREAM_KEY error",error);
    HANDLE activated=0;
    if (!error) {
        SetLastError(0);
        // The fourth argument is XXX_Init's second parameter, not its registry path.
        // This key is outside BuiltIn and holds no pointer or automatic startup rule.
        activated=ActivateDeviceEx(streamKey,0,0,d);
        error=activated?0:GetLastError();
    }
    InterlockedExchange(&pendingInit,0);
    SetLastError(error);
    return activated;
}
extern "C" DWORD CMX_Open(DWORD context, DWORD, DWORD) {
    Device *d = (Device *)context;
    if (d->gone || d->transitionRequested || InterlockedCompareExchange((LONG *)&d->opened, 1, 0) != 0) {
        usbTrace("OPEN_BUSY gone opened", d->gone, d->opened);
        SetLastError(ERROR_BUSY);
        return 0;
    }
    retain(d);
    usbTrace("OPEN_OK generation", d->info.generation);
    return context;
}
extern "C" BOOL CMX_Close(DWORD context) {
    Device *d = (Device *)context;
    EnterCriticalSection(&d->readLock);closeReceive(d);LeaveCriticalSection(&d->readLock);
    if (d->closedForTransition) SetEvent(d->closedForTransition);
    InterlockedExchange((LONG *)&d->opened, 0);
    release(d);
    return TRUE;
}
extern "C" DWORD CMX_Read(DWORD context, LPVOID buffer, DWORD size) {
    return readBulk((Device *)context, buffer, size);
}
extern "C" DWORD CMX_Write(DWORD context, LPCVOID buffer, DWORD size) {
    return transfer((Device *)context, FALSE, (void *)buffer, size);
}
extern "C" DWORD CMX_Seek(DWORD, LONG, DWORD) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return DWORD(-1);
}
extern "C" void CMX_PowerDown(DWORD) {}
extern "C" void CMX_PowerUp(DWORD) {}
// Never reset from USBDeviceAttach or while a stream IOCTL is in progress:
// CE's detach path can wait for these callbacks to finish. The worker owns a
// device reference and a loader reference until it exits, including on unplug.
static DWORD WINAPI transitionWorker(LPVOID context) {
    Device* d=(Device*)context;
    HMODULE pin=d->workerModule;
    BOOL ok=WaitForSingleObject(d->closedForTransition,3000)==WAIT_OBJECT_0;
    if (!ok) SetLastError(ERROR_TIMEOUT);
    if (ok && !d->gone && d->targetConfiguration) {
        HKEY key=0;
        LONG error=RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"Drivers\\USB\\LoadClients\\1452\\Default\\Default",0,
            KEY_QUERY_VALUE|KEY_SET_VALUE,&key);
        if (!error) {
            error=RegSetValueExW(key,L"bConfigurationValue",0,REG_DWORD,
                (const BYTE*)&d->targetConfiguration,sizeof(DWORD));
            DWORD type=0,size=sizeof(DWORD),value=0;
            if (!error) error=RegQueryValueExW(key,L"bConfigurationValue",0,&type,(BYTE*)&value,&size);
            if (!error && (type!=REG_DWORD || size!=sizeof(DWORD) || value!=d->targetConfiguration))
                error=ERROR_INVALID_DATA;
            RegCloseKey(key);
        }
        usbTrace("SELECT_CONFIG value error",d->targetConfiguration,error);
        ok=error==ERROR_SUCCESS;
        SetLastError(error);
    } else if (ok && !d->gone) {
        HANDLE event=CreateEventW(0,TRUE,FALSE,0);
        ok=FALSE;
        if (event) {
            USB_DEVICE_REQUEST request={0xc0,0x52,0,4,1};
            usbTrace("MODE_REQUEST type_request value_index length",0xc052,4,1);
            BYTE reply=0xff;DWORD count=0;
            USB_TRANSFER t=d->api->lpIssueVendorTransfer(d->usb,complete,event,
                USB_NO_WAIT|USB_IN_TRANSFER,&request,&reply,0);
            ok=finish(d,t,event,3000,&count) && count==1 && reply==0;
            usbTrace("CARPLAY_MODE result bytes reply",ok,count,reply);
            CloseHandle(event);
        }
    }
    if (ok && !d->gone) {
        // fReset=TRUE: re-enumerate this device, not the hub or other USB ports.
        ok=d->api->lpDisableDevice(d->usb,TRUE,0);
        usbTrace("REENUMERATE result error",ok,ok?0:GetLastError());
    }
    usbTrace("TRANSITION_DONE result detached",ok,d->gone);
    EnterCriticalSection(&d->writeLock);
    if (!ok) d->transitionRequested=FALSE;
    LeaveCriticalSection(&d->writeLock);
    release(d);
    FreeLibraryAndExitThread(pin,0);
    return 0;
}
static BOOL queueTransition(Device* d) {
    EnterCriticalSection(&d->writeLock);
    BOOL ok=d->transitionRequested;
    if (!ok && !d->gone) {
        // The tail of USB_FUNCS is optional on older OS builds.
        if (d->api->dwCount < DWORD((const BYTE*)&d->api->lpDisableDevice-(const BYTE*)d->api)+sizeof(void*) ||
            !d->api->lpDisableDevice) {
            SetLastError(ERROR_NOT_SUPPORTED);
        } else {
            WCHAR path[MAX_PATH];
            DWORD length=GetModuleFileNameW(module,path,MAX_PATH);
            typedef HMODULE (WINAPI *Loader)(LPCWSTR);
            HMODULE core=GetModuleHandleW(L"coredll.dll");
            Loader load=core?(Loader)GetProcAddress(core,L"LoadDriver"):0;
            if (!d->closedForTransition) d->closedForTransition=CreateEventW(0,TRUE,FALSE,0);
            if (d->closedForTransition && load && length && length<MAX_PATH) {
                ResetEvent(d->closedForTransition);
                d->workerModule=load(path);
                if (d->workerModule) {
                    retain(d);d->transitionRequested=TRUE;
                    HANDLE thread=CreateThread(0,0,transitionWorker,d,0,0);
                    ok=thread!=0;
                    if (thread) CloseHandle(thread);
                    else { d->transitionRequested=FALSE;release(d);FreeLibrary(d->workerModule); }
                }
            }
        }
    }
    usbTrace("TRANSITION_QUEUED result target",ok,d->targetConfiguration);
    LeaveCriticalSection(&d->writeLock);
    return ok;
}
extern "C" BOOL CMX_IOControl(DWORD context, DWORD code, PBYTE, DWORD, PBYTE out, DWORD size,
                              PDWORD returned) {
    Device *d = (Device *)context;
    if (returned)
        *returned = 0;
    if (d->gone) {
        SetLastError(ERROR_DEVICE_NOT_CONNECTED);
        return FALSE;
    }
    if (code == WINXPLAY_USB_QUERY) {
        if (!out || size < sizeof(d->info)) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        memcpy(out, &d->info, sizeof(d->info));
        if (returned)
            *returned = sizeof(d->info);
        return TRUE;
    }
    if (code == WINXPLAY_USB_CARPLAY_MODE) {
        if (d->info.ncmPresent)
            return TRUE;
        return queueTransition(d);
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
extern "C" BOOL USBDeviceAttach(USB_HANDLE usb, LPCUSB_FUNCS api, LPCUSB_INTERFACE iface, LPCWSTR,
                                LPBOOL accepted, LPCUSB_DRIVER_SETTINGS, DWORD) {
    usbTrace("ATTACH pid api interface", GetCurrentProcessId(), (DWORD)api, (DWORD)iface);
    if (accepted) *accepted = FALSE;
    if (!accepted || !api || !api->lpGetDeviceInfo) {
        usbTrace("REJECT_ARGUMENTS accepted api interface", (DWORD)accepted, (DWORD)api, (DWORD)iface);
        return FALSE;
    }
    LPCUSB_DEVICE desc = api->lpGetDeviceInfo(usb);
    usbTrace("DESCRIPTOR vendor product configurations", desc ? desc->Descriptor.idVendor : 0,
             desc ? desc->Descriptor.idProduct : 0, desc ? desc->Descriptor.bNumConfigurations : 0);
    if(desc)usbTrace("DEVICE_CLASS class subclass protocol",desc->Descriptor.bDeviceClass,
        desc->Descriptor.bDeviceSubClass,desc->Descriptor.bDeviceProtocol);
    if (!desc || desc->Descriptor.idVendor != 0x05ac || (iface &&
        (!usbNext(iface) || iface->Descriptor.bInterfaceClass != 0xff ||
         iface->Descriptor.bInterfaceSubClass != 0xfe || iface->Descriptor.bInterfaceProtocol != 2))) {
        usbTrace("REJECT_DESCRIPTOR");
        return FALSE;
    }
    DWORD activeFeatures=configurationFeatures(desc->lpActiveConfig);
    DWORD offeredFeatures=0;
    DWORD target=carPlayConfiguration(desc,offeredFeatures);
    if (!iface && !(offeredFeatures & (USB_HAS_MUX|USB_HAS_PTP))) {
        usbTrace("REJECT_BOOTSTRAP_NO_PHONE_INTERFACES");
        return FALSE;
    }
    // Whole-device matching happens before interface matching. A ready phone
    // proceeds through the host's normal interface loader (including NCM).
    if (!iface && (activeFeatures & 3)==3) {
        usbTrace("BOOTSTRAP_READY_DEFER_TO_INTERFACES");
        return FALSE;
    }
    // Log the actual active configuration before opening any pipe. This is
    // device information supplied by the USB host, not a registry preference.
    usbTrace("ACTIVE_CONFIG value interfaces interface_number",
        desc->lpActiveConfig ? desc->lpActiveConfig->Descriptor.bConfigurationValue : 0,
        desc->lpActiveConfig ? desc->lpActiveConfig->dwNumInterfaces : 0,
        iface ? iface->Descriptor.bInterfaceNumber : DWORD(-1));
    usbTrace("ABI device interface api_sizes",desc->dwCount,iface ? iface->dwCount : 0,api->dwCount);
    usbTrace("DRIVER_REV persistent_bulk_rx",1);
    const USB_ENDPOINT_DESCRIPTOR *in = 0, *out = 0;
    LPCUSB_ENDPOINT endpoint=iface ? iface->lpEndpoints : 0;
    for (unsigned i = 0; iface && i < iface->Descriptor.bNumEndpoints; ++i) {
        LPCUSB_ENDPOINT next=usbNext(endpoint);
        if (!next) break;
        const USB_ENDPOINT_DESCRIPTOR &e = endpoint->Descriptor;
        endpoint=next;
        if ((e.bmAttributes & 3) == 2) {
            if (e.bEndpointAddress & 0x80)
                in = &e;
            else
                out = &e;
        }
    }
    if (iface && (!in || !out)) {
        usbTrace("REJECT_ENDPOINTS in out", (DWORD)in, (DWORD)out);
        return FALSE;
    }
    Device *d = (Device *)LocalAlloc(LPTR, sizeof(Device));
    if (!d) {
        usbTrace("ALLOC_FAILED error", GetLastError());
        return FALSE;
    }
    d->refs = 1;
    d->usb = usb;
    d->api = api;
    d->bootstrap = iface == 0;
    d->targetConfiguration = target;
    InitializeCriticalSection(&d->readLock);
    InitializeCriticalSection(&d->writeLock);
    if (iface) {
        d->in = api->lpOpenPipe(usb, in);
        usbTrace("PIPE_IN handle error", (DWORD)d->in, d->in ? 0 : GetLastError());
        d->out = api->lpOpenPipe(usb, out);
        usbTrace("PIPE_OUT handle error", (DWORD)d->out, d->out ? 0 : GetLastError());
        if (!d->in || !d->out) {
            if (d->in)
                api->lpClosePipe(d->in);
            if (d->out)
                api->lpClosePipe(d->out);
            release(d);
            return FALSE;
        }
    }
    d->info.size = sizeof(d->info);
    d->info.version = 1;
    // Include attach time: CE may unload this DLL on unplug, resetting globals.
    // A new physical insertion must not reuse the previous denied-pair cookie.
    d->info.generation = GetTickCount() + InterlockedIncrement(&generation);
    d->info.vendor = 0x05ac;
    d->info.product = desc->Descriptor.idProduct;
    d->info.interfaceNumber = iface ? iface->Descriptor.bInterfaceNumber : DWORD(-1);
    d->info.bulkIn = in ? in->bEndpointAddress : 0;
    d->info.bulkOut = out ? out->bEndpointAddress : 0;
    if (desc->lpActiveConfig)
        d->info.configuration = desc->lpActiveConfig->Descriptor.bConfigurationValue;
    d->info.ncmPresent = !d->bootstrap && (activeFeatures & USB_HAS_NCM) != 0;
    usbTrace("BOOTSTRAP control_only target active_features",d->bootstrap,target,activeFeatures);
    WCHAR path[MAX_PATH];
    DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    usbTrace("ACTIVATE_CMX_BEGIN pid context path_length", GetCurrentProcessId(), (DWORD)d, length);
    if (length && length < MAX_PATH)
        d->registered = activateStream(d,path);
    else SetLastError(ERROR_BAD_PATHNAME);
    usbTrace("ACTIVATE_CMX handle error path_length", (DWORD)d->registered,
             d->registered ? 0 : GetLastError(), length);
    BOOL notified = d->registered && api->lpRegisterNotificationRoutine(usb, notify, d);
    if (!notified) {
        usbTrace("REJECT_REGISTRATION_OR_NOTIFY error", GetLastError());
        if (d->registered)
            DeactivateDevice(d->registered);
        if (d->in) api->lpClosePipe(d->in);
        if (d->out) api->lpClosePipe(d->out);
        release(d);
        return FALSE;
    }
    *accepted = TRUE;
    usbTrace("ACCEPTED configuration interface ncm", d->info.configuration,
             d->info.interfaceNumber, d->info.ncmPresent);
    return TRUE;
}
static USB_DRIVER_SETTINGS settings() {
    USB_DRIVER_SETTINGS s;
    memset(&s, 0xff, sizeof(s));
    s.dwCount = sizeof(s);
    s.dwVendorId = 0x05ac;
    s.dwInterfaceClass = 0xff;
    s.dwInterfaceSubClass = 0xfe;
    s.dwInterfaceProtocol = 2;
    return s;
}
#include "registration.h"
extern "C" BOOL WINAPI DllMain(HANDLE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        module = (HMODULE)instance;
    return TRUE;
}
