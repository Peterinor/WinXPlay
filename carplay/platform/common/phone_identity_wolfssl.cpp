#include "phone_pair.h"
#include "native_crypto.h"
#include "diagnostics.h"
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/asn.h>
#include <wolfssl/wolfcrypt/rsa.h>
#include <wolfssl/wolfcrypt/random.h>
#include <sstream>
namespace cp { namespace {
struct Random {
    WC_RNG value;
    Random(){require(wc_InitRng(&value)==0,"Pairing random initialization failed");}
    ~Random(){wc_FreeRng(&value);}
};
struct Key {
    RsaKey value;
    Key(){require(wc_InitRsaKey(&value,0)==0,"Pairing RSA initialization failed");}
    ~Key(){wc_FreeRsaKey(&value);}
};
Bytes pem(const Bytes &der,int type){
    Bytes out(8192);int n=wc_DerToPem(&der[0],der.size(),&out[0],out.size(),type);
    require(n>0,"Pairing PEM encoding failed");out.resize(n);return out;
}
Bytes privateKey(Key &key){
    Bytes der(4096);int n=wc_RsaKeyToDer(&key.value,&der[0],der.size());
    require(n>0,"Pairing private key encoding failed");der.resize(n);
    Bytes out=pem(der,RSA_TYPE);eraseSecret(der);return out;
}
std::string uuid(Random &rng){
    Bytes data(16);require(wc_RNG_GenerateBlock(&rng.value,&data[0],data.size())==0,"Pairing UUID failed");
    data[6]=(data[6]&15)|64;data[8]=(data[8]&63)|128;
    std::string s=hex(data);
    for(size_t i=0;i<s.size();++i)if(s[i]>='a' && s[i]<='f')s[i]-='a'-'A';
    return s.substr(0,8)+"-"+s.substr(8,4)+"-"+s.substr(12,4)+"-"+s.substr(16,4)+"-"+s.substr(20);
}
Bytes certificate(Key &subject,Key &root,Random &rng,bool ca,bool device){
    // Heap allocation keeps the small CE worker stack clear of large Cert fields.
    Cert *c=new Cert;Bytes der(8192);
    try {
        require(wc_InitCert(c)==0,"Pairing certificate initialization failed");
        c->version=2;c->serialSz=1;c->serial[0]=0;c->sigType=CTC_SHA256wRSA;
        c->isCA=ca?1:0;c->basicConstSet=1;c->basicConstCrit=1;
        c->selfSigned=ca?1:0;
        // CE units may boot with an unset clock. These local trust certificates
        // use an explicit validity interval rather than the car's wall clock.
        const byte before[]={0x17,13,'0','0','0','1','0','1','0','0','0','0','0','0','Z'};
        const byte after[]={0x17,13,'4','9','1','2','3','1','2','3','5','9','5','9','Z'};
        memcpy(c->beforeDate,before,sizeof(before));c->beforeDateSz=sizeof(before);
        memcpy(c->afterDate,after,sizeof(after));c->afterDateSz=sizeof(after);
        if(!ca)require(wc_SetKeyUsage(c,"digitalSignature,keyEncipherment")==0,"Pairing key usage failed");
        if(device)require(wc_SetSubjectKeyIdFromPublicKey(c,&subject.value,0)==0,"Pairing key identifier failed");
        int n=wc_MakeCert(c,&der[0],der.size(),&subject.value,0,&rng.value);
        require(n>0,"Pairing certificate construction failed");
        n=wc_SignCert(n,c->sigType,&der[0],der.size(),&root.value,0,&rng.value);
        require(n>0,"Pairing certificate signature failed");der.resize(n);
        Bytes out=pem(der,CERT_TYPE);delete c;return out;
    }catch(...){delete c;throw;}
}
struct PairCert {
    Bytes der;DecodedCert *decoded;Key key;
    PairCert(const Bytes &data):der(8192),decoded(0){
        require(!data.empty() && data.size()<=8192,"Pairing certificate size invalid");
        int n=wc_CertPemToDer(&data[0],data.size(),&der[0],der.size(),CERT_TYPE);
        require(n>0,"Pairing certificate PEM invalid");der.resize(n);
        decoded=new DecodedCert;wc_InitDecodedCert(decoded,&der[0],der.size(),0);
        int rc=wc_ParseCert(decoded,CERT_TYPE,NO_VERIFY,0);bool parsed=rc==0;word32 at=0;
        if(!rc)rc=wc_RsaPublicKeyDecode(decoded->publicKey,&at,&key.value,decoded->pubKeySize);
        if(rc){wc_FreeDecodedCert(decoded);delete decoded;decoded=0;
            std::ostringstream msg;msg<<"Pairing "<<(parsed?"RSA key decode":"certificate parse")<<" failed (wolfSSL "<<rc<<")";throw std::runtime_error(msg.str());}
    }
    ~PairCert(){if(decoded){wc_FreeDecodedCert(decoded);delete decoded;}}
};
Bytes publicDer(Key &key){
    Bytes result(4096);int n=wc_RsaKeyToPublicDer(&key.value,&result[0],result.size());
    require(n>0,"Pairing public key encoding failed");result.resize(n);return result;
}
void privateMatches(const Bytes &data,Key &expected){
    require(!data.empty() && data.size()<=8192,"Pairing private key size invalid");
    Bytes der(8192);int n=wc_KeyPemToDer(&data[0],data.size(),&der[0],der.size(),0);
    require(n>0,"Pairing private key PEM invalid");Key key;word32 at=0;
    int rc=wc_RsaPrivateKeyDecode(&der[0],&at,&key.value,n);eraseSecret(der);
    require(rc==0 && publicDer(key)==publicDer(expected),"Pairing certificate/private key mismatch");
}
void signatureMatches(PairCert &cert,Key &root){
    DecodedCert &c=*cert.decoded;
    require(c.signatureOID==CTC_SHA256wRSA && c.sigIndex>c.certBegin && c.sigIndex<=cert.der.size(),
        "Pairing certificate signature format invalid");
    Bytes tbs(cert.der.begin()+c.certBegin,cert.der.begin()+c.sigIndex);
    AirCrypto crypto(L"");Bytes digest=crypto.hash("SHA256",tbs);
    const U8 prefix[]={0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20};
    Bytes expected(prefix,prefix+sizeof(prefix));expected.insert(expected.end(),digest.begin(),digest.end());
    Bytes actual(512);int n=wc_RsaSSL_Verify(c.signature,c.sigLength,&actual[0],actual.size(),&root.value);
    require(n==int(expected.size()) && memcmp(&actual[0],&expected[0],expected.size())==0,
        "Pairing certificate RSA signature invalid");
}
}
void validatePhoneIdentity(const Value &pair,const Bytes &phonePublicKey){
    // Validate the record read back from CE storage, not only fresh test material.
    // No trust certificates or private keys are written to diagnostic logs.
    const char *stage="root";
    try{
        PairCert root(pair.at("RootCertificate").data);
        signatureMatches(root,root.key);privateMatches(pair.at("RootPrivateKey").data,root.key);
        stage="host";PairCert host(pair.at("HostCertificate").data);
        signatureMatches(host,root.key);privateMatches(pair.at("HostPrivateKey").data,host.key);
        stage="device";PairCert device(pair.at("DeviceCertificate").data);signatureMatches(device,root.key);
        Bytes der(8192);require(!phonePublicKey.empty() && phonePublicKey.size()<=8192,"Phone public key size invalid");
        int n=wc_PubKeyPemToDer(&phonePublicKey[0],phonePublicKey.size(),&der[0],der.size());
        require(n>0,"Phone public key PEM invalid");Key phone;word32 at=0;
        require(wc_RsaPublicKeyDecode(&der[0],&at,&phone.value,n)==0 && publicDer(phone)==publicDer(device.key),
            "Pairing device certificate does not match connected iPhone");
        diagnosticEvent("PAIR_CERTIFICATES_VALIDATED","rsa_sha256=3 private_key_matches=2 device_key_match=1");
    }catch(const std::exception &e){diagnosticEvent("PAIR_CERTIFICATE_INVALID",std::string(stage)+" "+e.what());throw;}
}
Value generatePhoneIdentity(const Bytes &publicKey){
    require(!publicKey.empty() && publicKey.size()<8192,"Invalid iPhone public key size");
    Random rng;Key root,host,device;
    Bytes der(4096);int n=wc_PubKeyPemToDer(&publicKey[0],publicKey.size(),&der[0],der.size());
    require(n>0,"Invalid iPhone public key PEM");word32 at=0;
    require(wc_RsaPublicKeyDecode(&der[0],&at,&device.value,n)==0 && at==unsigned(n),"Invalid iPhone RSA public key");
    require(wc_MakeRsaKey(&root.value,2048,65537,&rng.value)==0 &&
            wc_MakeRsaKey(&host.value,2048,65537,&rng.value)==0,"Pairing RSA generation failed");
    Value pair=Value::dictionary();
    pair.dict["HostID"]=Value::str(uuid(rng));pair.dict["SystemBUID"]=Value::str(uuid(rng));
    pair.dict["RootCertificate"]=Value::blob(certificate(root,root,rng,true,false));
    pair.dict["HostCertificate"]=Value::blob(certificate(host,root,rng,false,false));
    pair.dict["DeviceCertificate"]=Value::blob(certificate(device,root,rng,false,true));
    pair.dict["RootPrivateKey"]=Value::blob(privateKey(root));
    pair.dict["HostPrivateKey"]=Value::blob(privateKey(host));
    return pair;
}
}
