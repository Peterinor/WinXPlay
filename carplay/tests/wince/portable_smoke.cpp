#include "../../platform/wince/portable.h"
#include "../../platform/common/native_crypto.h"
#include "../../platform/common/local_identity.h"
using namespace cp;
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
    Value result=Value::dictionary();std::wstring root=executableDirectory();
    try {
        bool rejected=false;preparePortableReceiver();
        require(parsePlist(readFile(root+L"/portable-startup.plist")).at("pairing_on_connection").boolean,"Fresh startup needs an imported record");
        require(GetFileAttributesW((root+L"\\driver-called").c_str())!=INVALID_FILE_ATTRIBUTES,"Fresh startup did not register USB");
        Value pair=Value::dictionary();pair.dict["DeviceUDID"]=Value::str("SYNTHETIC-TEST");
        pair.dict["HostID"]=Value::str("TEST-HOST");pair.dict["SystemBUID"]=Value::str("TEST-SYSTEM");
        const char *keys[]={"RootPrivateKey","RootCertificate","DeviceCertificate"};
        for(unsigned i=0;i<3;++i)pair.dict[keys[i]]=Value::blob(bytes("synthetic-only"));
        Bytes plain=xmlPlist(pair);writeFile(root+L"/phone-pair.plist",plain);
        preparePortableReceiver();Bytes first=readFile(root+L"/phone-pair.dat");
        require(first!=plain && protectedRead(root+L"/phone-pair.dat")==plain,"Encrypted import failed");
        preparePortableReceiver();require(readFile(root+L"/phone-pair.dat")==first,"Unchanged record rewritten");
        writeFile(root+L"/phone-pair.dat",bytes("broken"));preparePortableReceiver();
        require(protectedRead(root+L"/phone-pair.dat")==plain,"Pair recovery failed");
        DeleteFileW((root+L"\\phone-pair.plist").c_str());preparePortableReceiver();
        pair.dict.erase("SystemBUID");writeFile(root+L"/phone-pair.plist",xmlPlist(pair));
        rejected=false;try{preparePortableReceiver();}catch(...){rejected=true;}
        require(rejected && protectedRead(root+L"/phone-pair.dat")==plain,"Invalid replacement damaged prior record");
        DeleteFileW((root+L"\\phone-pair.plist").c_str());
        writeFile(root+L"/driver-fail",bytes("test"));
        rejected=false;try{preparePortableReceiver();}catch(...){rejected=true;}
        require(rejected,"Driver registration failure ignored");
        DeleteFileW((root+L"\\driver-fail").c_str());preparePortableReceiver();
        require(parsePlist(readFile(root+L"/portable-startup.plist")).at("ready").boolean,"Final startup failed");
        Bytes secret=readFile(root+L"/identity/identity.pk8");
        writeFile(root+L"/identity/identity.pk8",readFile(root+L"/mismatched.pk8"));
        rejected=false;try{LocalAuth mismatch(root+L"/identity",L"");}catch(...){rejected=true;}
        writeFile(root+L"/identity/identity.pk8",secret);eraseSecret(secret);
        require(rejected,"Mismatched key with critical certificate extension accepted");
        Bytes leaf=readFile(root+L"/identity/certificate.der"),bad=leaf;bad.push_back(0);
        writeFile(root+L"/identity/certificate.der",bad);
        rejected=false;try{LocalAuth malformed(root+L"/identity",L"");}catch(...){rejected=true;}
        writeFile(root+L"/identity/certificate.der",leaf);require(rejected,"Trailing certificate accepted");
        result.dict["passed"]=Value::flag(true);result.dict["scenarios"]=Value::num(9);
        result.dict["critical_accessory_extension"]=Value::flag(true);
    }catch(const std::exception &e){result.dict["passed"]=Value::flag(false);result.dict["error"]=Value::str(e.what());}
    result.dict["usb_registration_stubbed"]=Value::flag(true);
    result.dict["ncm_prerequisites_stubbed"]=Value::flag(true);
    result.dict["physical_iphone_verified"]=Value::flag(false);
    writeFile(root+L"/portable-smoke.plist",xmlPlist(result));
    return result.at("passed").boolean?0:1;
}
