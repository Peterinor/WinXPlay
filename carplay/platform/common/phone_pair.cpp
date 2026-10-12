#include "phone_pair.h"
#include "native_crypto.h"
#include "diagnostics.h"
namespace cp { namespace {
std::string identityTag(const std::string &id){
    try{AirCrypto crypto(L"");return hex(crypto.hash("SHA256",bytes(id))).substr(0,16);}
    catch(...){return "unavailable";}
}
std::string pairTag(const Value &v){return identityTag(v.at("HostID").string);}
bool valid(const Value &v,const std::string &udid){
    const char *fields[]={"RootCertificate","RootPrivateKey","DeviceCertificate"};
    if(v.type!=Value::Dict || !v.dict.count("DeviceUDID") || !v.dict.count("HostID") || !v.dict.count("SystemBUID"))return false;
    if(v.at("DeviceUDID").string!=udid || v.at("HostID").string.empty() || v.at("SystemBUID").string.empty())return false;
    for(unsigned i=0;i<3;++i)if(!v.dict.count(fields[i]) || v.at(fields[i]).data.empty())return false;
    return true;
}
Value load(const std::wstring &path,const std::string &udid,const char *slot){
    DWORD attr=GetFileAttributesW(path.c_str()),error=attr==INVALID_FILE_ATTRIBUTES?GetLastError():0;
    if(attr==INVALID_FILE_ATTRIBUTES && (error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND) &&
       GetFileAttributesW((path+L".bak").c_str())==INVALID_FILE_ATTRIBUTES){
        diagnosticEvent("PAIR_STORE_ABSENT",slot);return Value();
    }
    try{Value v=parsePlist(protectedRead(path));
        if(valid(v,udid)){diagnosticEvent("PAIR_STORE_REUSED",std::string(slot)+" host_tag="+pairTag(v));return v;}
        diagnosticEvent("PAIR_STORE_INVALID",slot);
    }catch(const std::exception &e){diagnosticEvent("PAIR_STORE_READ_FAILED",std::string(slot)+" "+e.what());}
    return Value();
}
void save(const std::wstring &path,const Value &v,const char *slot){
    diagnosticEvent("PAIR_STORE_WRITE_BEGIN",slot);
    try{
        Bytes data=xmlPlist(v);protectedWrite(path,data);
        bool ok=protectedRead(path)==data;eraseSecret(data);require(ok,"Pairing record readback failed");
        diagnosticEvent("PAIR_STORE_WRITE_VERIFIED",std::string(slot)+" host_tag="+pairTag(v));
    }catch(const std::exception &e){diagnosticEvent("PAIR_STORE_WRITE_FAILED",std::string(slot)+" "+e.what());throw;}
}
Value request(const char *name){
    Value v=Value::dictionary();v.dict["Request"]=Value::str(name);v.dict["Label"]=Value::str("WinXPlay");return v;
}
void check(const Value &v,const char *name){
    if(v.dict.count("Error"))throw std::runtime_error("Lockdown pairing: "+v.at("Error").string);
    require(v.at("Request").string==name,"Unexpected Lockdown pairing response");
    require(!v.dict.count("Result") || v.at("Result").string=="Success","Lockdown pairing failed");
}
}
std::wstring phonePairPath(const std::wstring &folder,const std::string &udid){
    require(!udid.empty() && udid.size()<=80,"Invalid iPhone identifier");
    for(size_t i=0;i<udid.size();++i)require((udid[i]>='0'&&udid[i]<='9') ||
        (udid[i]>='a'&&udid[i]<='f') || (udid[i]>='A'&&udid[i]<='F') || udid[i]=='-',"Invalid iPhone identifier");
    return folder+L"\\phone-pair-"+wide(udid)+L".dat";
}
Value ensurePhonePair(PhonePairChannel &channel,const std::wstring &folder,const std::string &udid){
    const std::wstring path=phonePairPath(folder,udid),pending=path+L".pending";
    diagnosticEvent("PAIR_BEGIN","device_tag="+identityTag(udid));
    Value pair=load(path,udid,"active");if(valid(pair,udid))return pair;
    // Optional previously imported desktop trust; fresh packages need none.
    if(GetFileAttributesW((path+L".rejected").c_str())==INVALID_FILE_ATTRIBUTES){
        pair=load(folder+L"\\phone-pair.dat",udid,"legacy");
        if(valid(pair,udid)){save(path,pair,"active_imported");return pair;}
    }
    pair=load(pending,udid,"pending");
    Value get=request("GetValue");get.dict["Key"]=Value::str("DevicePublicKey");
    Value response=channel.exchange(get);check(response,"GetValue");
    require(response.at("Value").type==Value::Data,"iPhone did not provide its public key; unlock it");
    if(!valid(pair,udid) || !pair.dict.count("HostCertificate") || pair.at("HostCertificate").data.empty()){
        diagnosticEvent("PAIR_IDENTITY_GENERATE_BEGIN");
        pair=generatePhoneIdentity(response.at("Value").data);
        diagnosticEvent("PAIR_IDENTITY_GENERATED","host_tag="+pairTag(pair));
        pair.dict["DeviceUDID"]=Value::str(udid);
        // Persist before Pair so retries keep the identity shown by the trust dialog.
        save(pending,pair,"pending");
    }
    // DiPlay's controller and Apple's host identities use uppercase UUID text.
    // Canonicalize only unpaired records, retaining the UUID values and all keys.
    bool normalized=false;const char *ids[]={"HostID","SystemBUID"};
    for(unsigned j=0;j<2;++j){std::string &id=pair.dict[ids[j]].string;
        for(size_t i=0;i<id.size();++i)if(id[i]>='a' && id[i]<='f'){id[i]-='a'-'A';normalized=true;}}
    if(normalized){save(pending,pair,"pending_uuid_normalized");diagnosticEvent("PAIR_UUID_NORMALIZED");}
    validatePhoneIdentity(pair,response.at("Value").data);
    // Match DiPlay's pre-pair handshake: associate the current Lockdown channel
    // with the same persistent BUID that will be submitted in PairRecord.
    Value announce=request("SetValue");announce.dict["Key"]=Value::str("UntrustedHostBUID");
    announce.dict["Value"]=pair.at("SystemBUID");
    diagnosticEvent("PAIR_HOST_BUID_BEGIN","host_tag="+pairTag(pair));
    Value announced=channel.exchange(announce);check(announced,"SetValue");
    diagnosticEvent("PAIR_HOST_BUID_SET");
    Value publicPair=Value::dictionary();
    const char *fields[]={"DeviceCertificate","HostCertificate","RootCertificate","HostID","SystemBUID"};
    for(unsigned i=0;i<5;++i)publicPair.dict[fields[i]]=pair.at(fields[i]);
    Value pairRequest=request("Pair");pairRequest.dict["PairRecord"]=publicPair;
    pairRequest.dict["ProtocolVersion"]=Value::str("2");
    Value options=Value::dictionary();options.dict["ExtendedPairingErrors"]=Value::flag(true);
    pairRequest.dict["PairingOptions"]=options;
    diagnosticEvent("PAIR_REQUEST_BEGIN","host_tag="+pairTag(pair));
    response=channel.exchange(pairRequest);
    diagnosticEvent("PAIR_RESPONSE",response.dict.count("Error")?response.at("Error").string:"no_error");
    check(response,"Pair");
    if(response.dict.count("EscrowBag") && response.at("EscrowBag").type==Value::Data)pair.dict["EscrowBag"]=response.at("EscrowBag");
    save(path,pair,"active");
    diagnosticEvent("PAIR_COMPLETE",DeleteFileW(pending.c_str())?"pending_removed":"pending_cleanup_failed");return pair;
}
void invalidatePhonePair(const std::wstring &folder,const std::string &udid){
    diagnosticEvent("PAIR_INVALIDATE","phone rejected StartSession identity");
    std::wstring path=phonePairPath(folder,udid);
    writeFile(path+L".rejected",bytes("Rejected by iPhone; do not reimport previous trust"));
    if(GetFileAttributesW((path+L".bak").c_str())!=INVALID_FILE_ATTRIBUTES)
        require(DeleteFileW((path+L".bak").c_str())!=FALSE,"Cannot remove rejected pairing backup");
    if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)
        require(DeleteFileW(path.c_str())!=FALSE,"Cannot remove rejected phone pairing");
}
}
