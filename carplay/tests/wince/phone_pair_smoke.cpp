#include "../../platform/common/phone_pair.h"
#include "../../platform/common/native_crypto.h"
#include <wolfssl/ssl.h>
using namespace cp;
namespace {
class Phone : public PhonePairChannel {
  public:
    Bytes key;std::string error,setError,buid;unsigned getCount,pairCount,setCount;Value last;
    Phone():getCount(0),pairCount(0),setCount(0){}
    Value exchange(const Value &r){
        Value response=Value::dictionary();response.dict["Request"]=r.at("Request");
        if(r.at("Request").string=="GetValue"){
            require(r.at("Key").string=="DevicePublicKey","Unexpected value query");
            ++getCount;response.dict["Value"]=Value::blob(key);return response;
        }
        if(r.at("Request").string=="SetValue"){
            require(r.at("Key").string=="UntrustedHostBUID","Unexpected pairing host registration");
            ++setCount;buid=r.at("Value").string;
            if(!setError.empty())response.dict["Error"]=Value::str(setError);
            return response;
        }
        require(r.at("Request").string=="Pair","Unexpected pairing verb");
        ++pairCount;last=r;
        const Value &p=r.at("PairRecord");
        require(!buid.empty() && buid==p.at("SystemBUID").string,"Pair sent before matching host BUID");buid.clear();
        require(p.dict.size()==5 && !p.dict.count("RootPrivateKey") && !p.dict.count("HostPrivateKey"),"Private data on pairing wire");
        require(r.at("ProtocolVersion").string=="2" && r.at("PairingOptions").at("ExtendedPairingErrors").boolean,"Missing pairing options");
        if(!error.empty())response.dict["Error"]=Value::str(error);
        else response.dict["EscrowBag"]=Value::blob(bytes("synthetic-escrow"));
        return response;
    }
};
void failed(Phone &p,const std::wstring &root,const std::string &id,const char *expected){
    bool rejected=false;try{ensurePhonePair(p,root,id);}catch(const std::exception &e){
        if(std::string(e.what()).find(expected)==std::string::npos)throw;
        rejected=true;
    }
    require(rejected,"Pairing failure/pending was not preserved");
}
void tlsLoad(const Value &pair){
    require(wolfSSL_Init()==WOLFSSL_SUCCESS,"TLS test initialization failed");
    WOLFSSL_CTX *ctx=wolfSSL_CTX_new(wolfTLSv1_2_client_method());
    require(ctx!=0,"TLS test context failed");
    const Bytes &cert=pair.at("RootCertificate").data,&key=pair.at("RootPrivateKey").data;
    bool ok=wolfSSL_CTX_use_certificate_buffer(ctx,&cert[0],cert.size(),WOLFSSL_FILETYPE_PEM)==WOLFSSL_SUCCESS &&
        wolfSSL_CTX_use_PrivateKey_buffer(ctx,&key[0],key.size(),WOLFSSL_FILETYPE_PEM)==WOLFSSL_SUCCESS &&
        wolfSSL_CTX_check_private_key(ctx)==WOLFSSL_SUCCESS;
    wolfSSL_CTX_free(ctx);wolfSSL_Cleanup();
    require(ok,"TLS rejected local pairing certificate/key");
}
}
#ifdef UNDER_CE
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
#else
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
#endif
    Value report=Value::dictionary();std::wstring root=executableDirectory();DWORD begin=GetTickCount();
    try{
        Phone phone;phone.key=readFile(root+L"/device-public.pem");
        const std::string id="00000000-0123456789ABCDEF";
        std::wstring path=phonePairPath(root,id);
        phone.error="PairingDialogResponsePending";
        failed(phone,root,id,"PairingDialogResponsePending");
        require(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES,"Pending trust marked active");
        Bytes pending=protectedRead(path+L".pending");
        Value first=parsePlist(pending);require(first.at("HostID").string.size()==36,"Host UUID invalid");
        require(readFile(path+L".pending")!=pending,"Pairing secrets not encrypted");
        failed(phone,root,id,"PairingDialogResponsePending");
        require(phone.getCount==2 && protectedRead(path+L".pending")==pending,"Pending retry regenerated host identity");
        Value lower=first;const char *ids[]={"HostID","SystemBUID"};
        for(unsigned j=0;j<2;++j){std::string &s=lower.dict[ids[j]].string;
            for(size_t i=0;i<s.size();++i)if(s[i]>='A' && s[i]<='F')s[i]+='a'-'A';}
        protectedWrite(path+L".pending",xmlPlist(lower));
        failed(phone,root,id,"PairingDialogResponsePending");
        require(protectedRead(path+L".pending")==pending,"UUID normalization replaced key material or failed persistence");
        Value corrupt=first;corrupt.dict["DeviceCertificate"]=first.at("HostCertificate");
        protectedWrite(path+L".pending",xmlPlist(corrupt));unsigned rejectedCalls=phone.pairCount;
        failed(phone,root,id,"does not match connected iPhone");
        require(phone.pairCount==rejectedCalls,"Wrong device certificate was sent to phone");
        corrupt=first;corrupt.dict["HostPrivateKey"]=first.at("RootPrivateKey");
        protectedWrite(path+L".pending",xmlPlist(corrupt));
        failed(phone,root,id,"certificate/private key mismatch");
        require(phone.pairCount==rejectedCalls,"Mismatched host key was sent to phone");
        corrupt=first;Bytes &cert=corrupt.dict["HostCertificate"].data;
        // Alter a base64 character in the signature, leaving the DER parseable.
        size_t tail=cert.size()-60;cert[tail]=cert[tail]=='A'?'B':'A';
        protectedWrite(path+L".pending",xmlPlist(corrupt));
        failed(phone,root,id,"RSA signature invalid");
        require(phone.pairCount==rejectedCalls,"Damaged signature was sent to phone");
        protectedWrite(path+L".pending",pending);
        phone.error="UserDeniedPairing";failed(phone,root,id,"UserDeniedPairing");
        require(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES,"Denied trust marked active");
        phone.error="PasswordProtected";failed(phone,root,id,"PasswordProtected");
        unsigned before=phone.pairCount;
        phone.setError="SetProhibited";failed(phone,root,id,"SetProhibited");phone.setError.clear();
        require(phone.pairCount==before && protectedRead(path+L".pending")==pending,"Failed BUID registration sent Pair or replaced identity");
        phone.error="InvalidPairRecord";failed(phone,root,id,"InvalidPairRecord");
        require(protectedRead(path+L".pending")==pending,"Rejected Pair regenerated pending identity");
        phone.error.clear();Value pair=ensurePhonePair(phone,root,id);
        tlsLoad(pair);
        require(pair.at("HostID").string==first.at("HostID").string && pair.at("EscrowBag").data==bytes("synthetic-escrow"),"Successful pairing lost identity/escrow");
        require(GetFileAttributesW((path+L".pending").c_str())==INVALID_FILE_ATTRIBUTES,"Pending store not cleaned");
        unsigned calls=phone.pairCount;
        ensurePhonePair(phone,root,id);require(phone.pairCount==calls,"Reconnect repeated trust dialog");
        // Only synthetic test keys are exported for independent certificate validation.
        writeFile(root+L"/generated-pair.plist",xmlPlist(pair));
        writeFile(root+L"/pair-request.plist",xmlPlist(phone.last));
        protectedWrite(root+L"/phone-pair.dat",xmlPlist(pair));
        protectedWrite(path,xmlPlist(pair)); // Leave .bak to test revoked-trust recovery.
        invalidatePhonePair(root,id);
        require(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES && GetFileAttributesW((path+L".bak").c_str())==INVALID_FILE_ATTRIBUTES,"Rejected trust can revive from backup");
        // Reuse synthetic key material to isolate the retry state test from RSA time.
        protectedWrite(path+L".pending",pending);
        phone.error="PairingDialogResponsePending";failed(phone,root,id,"PairingDialogResponsePending");
        require(phone.pairCount==calls+1,"Rejected legacy trust was reimported");
        phone.error.clear();ensurePhonePair(phone,root,id);
        bool invalid=false;try{phonePairPath(root,"../invalid");}catch(...){invalid=true;}
        require(invalid,"Unsafe device identifier accepted");
#ifdef WINXPLAY_DIAGNOSTICS_TEST
        // Verify that the actual pairing path emits useful evidence without copying trust data.
        Bytes trace=readFile(root+L"/connection-trace.log");std::string text(trace.begin(),trace.end());
        require(text.find("PAIR_STORE_REUSED active")!=std::string::npos &&
                text.find("PAIR_STORE_REUSED pending")!=std::string::npos &&
                text.find("PAIR_STORE_WRITE_VERIFIED active")!=std::string::npos &&
                text.find("PAIR_RESPONSE PairingDialogResponsePending")!=std::string::npos,
                "Pairing trace missing transition evidence");
        require(text.find(pair.at("HostID").string)==std::string::npos &&
                text.find("PRIVATE KEY")==std::string::npos && text.find(id)==std::string::npos,
                "Pairing trace exposes identity material");
        writeFile(path,bytes("synthetic corrupt encrypted record"));
        protectedWrite(path+L".pending",pending);phone.error="PairingDialogResponsePending";
        failed(phone,root,id,"PairingDialogResponsePending");
        trace=readFile(root+L"/connection-trace.log");text.assign(trace.begin(),trace.end());
        require(text.find("PAIR_STORE_READ_FAILED active")!=std::string::npos,
                "Corrupt active record read failure was hidden");
#endif
        report.dict["passed"]=Value::flag(true);report.dict["scenarios"]=Value::num(16);
    }catch(const std::exception &e){report.dict["passed"]=Value::flag(false);report.dict["error"]=Value::str(e.what());}
    report.dict["elapsed_ms"]=Value::num(DWORD(GetTickCount()-begin));
    report.dict["transport_stubbed"]=Value::flag(true);report.dict["physical_iphone_verified"]=Value::flag(false);
    writeFile(root+L"/phone-pair-smoke.plist",xmlPlist(report));return report.at("passed").boolean?0:1;
}
