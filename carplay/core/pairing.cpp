#include "pairing.h"
namespace cp {
namespace {
Bytes modulus() {
    return unhex("FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74"
                 "020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F1437"
                 "4FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
                 "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF05"
                 "98DA48361C55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB"
                 "9ED529077096966D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B"
                 "E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955817183"
                 "995497CEA956AE515D2261898FA051015728E5A8AAAC42DAD33170D04507A33A"
                 "85521ABDF1CBA64ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7AB"
                 "F5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6BF12FFA06D98A0864D8"
                 "7602733EC86A64521F2B18177B200CBBE117577A615D6C770988C0BAD946E208"
                 "E24FA074E5AB3143DB5BFCE0FD108E4B82D120A93AD2CAFFFFFFFFFFFFFFFF");
}
Bytes pad(const Bytes &b) {
    require(b.size() <= 384, "SRP public key exceeds group");
    Bytes p(384 - b.size(), 0);
    return join(p, b);
}
Bytes minimal(const Bytes &b) {
    size_t p = 0;
    while (p + 1 < b.size() && b[p] == 0)
        ++p;
    return slice(b, p, b.size() - p);
}
Bytes nonce(const char *s) {
    return join(Bytes(4, 0), bytes(s));
}
const Bytes &field(const std::map<U8, Bytes> &m, U8 type) {
    std::map<U8, Bytes>::const_iterator i = m.find(type);
    require(i != m.end(), "Missing pairing field");
    return i->second;
}
bool equal(const Bytes &a, const Bytes &b) {
    if (a.size() != b.size())
        return false;
    U8 v = 0;
    for (size_t i = 0; i < a.size(); ++i)
        v |= a[i] ^ b[i];
    return v == 0;
}
} // namespace
Pairing::Pairing(CryptoProvider &c, PairingStore &store)
    : c_(c), store_(store), setupState_(0), verifyState_(0) {
    if (store_.exists())
        stored_ = parsePlist(store_.read());
    else {
        stored_ = Value::dictionary();
        stored_.dict["seed"] = Value::blob(c_.random(32));
        stored_.dict["peers"] = Value::dictionary();
        store_.write(binaryPlist(stored_));
    }
    seed_ = stored_.at("seed").data;
    require(seed_.size() == 32 && stored_.at("peers").type == Value::Dict,
            "Invalid saved pairing identity");
    // The persisted receiver key is generated separately on each installation.
    // Derive all public accessory IDs from its PUBLIC key, never from the MFi
    // certificate or phone HostID. Existing pairing keys/peers stay unchanged.
    Bytes pub=publicKey();require(pub.size()==32,"Invalid receiver public key");
    serial_="WXP-"+hex(slice(pub,0,16));
    Bytes mac=slice(pub,0,6);mac[0]=U8((mac[0]&0xfc)|2); // local unicast ID
    std::string h=hex(mac);
    for(unsigned i=0;i<6;++i){if(i)identifier_+=':';identifier_+=h.substr(i*2,2);}
    Bytes display=slice(pub,16,16);display[6]=U8((display[6]&15)|0x40);display[8]=U8((display[8]&63)|0x80);
    h=hex(display);displayUuid_=h.substr(0,8)+"-"+h.substr(8,4)+"-"+h.substr(12,4)+"-"+h.substr(16,4)+"-"+h.substr(20);
}
Bytes Pairing::publicKey() {
    return c_.publicKey("ED25519", seed_);
}
Bytes Pairing::handle(bool verification, const Bytes &body) {
    unsigned state = 0;
    try {
        require(body.size() <= 4096, "Pairing message too large");
        std::map<U8, Bytes> t = parseTlv8(body);
        const Bytes &s = field(t, 6);
        require(s.size() == 1, "Pairing state size");
        state = s[0];
        return verification ? verify(t) : setup(t);
    } catch (const std::exception &) {
        if (verification) {
            verifyState_ = 0;
            sharedSecret.clear();
            readKey.clear();
            writeKey.clear();
        } else {
            setupState_ = 0;
            setupKey_.clear();
        }
        return join(tlv8(6, Bytes(1, U8(state + 1))), tlv8(7, Bytes(1, 2)));
    }
}
Bytes Pairing::setup(const std::map<U8, Bytes> &t) {
    unsigned state = field(t, 6)[0];
    Bytes n = modulus(), g(1, 5);
    if (state == 1) {
        setupState_ = 0;
        setupKey_.clear();
        salt_ = c_.random(16);
        exponent_ = c_.random(32);
        Bytes x = c_.hash("SHA512", join(salt_, c_.hash("SHA512", bytes("Pair-Setup:3939"))));
        verifier_ = c_.mod(g, x, n, '^');
        Bytes multiplier = c_.hash("SHA512", join(n, pad(g)));
        serverPublic_ =
            c_.mod(c_.mod(multiplier, verifier_, n, '*'), c_.mod(g, exponent_, n, '^'), n, '+');
        setupState_ = 2;
        return join(join(tlv8(6, Bytes(1, 2)), tlv8(3, serverPublic_)), tlv8(2, salt_));
    }
    if (state == 3) {
        require(setupState_ == 2, "SRP stage order");
        setupState_ = 0;
        Bytes a = pad(field(t, 3)), proof = field(t, 4);
        require(c_.mod(a, g, n, '%') != Bytes(384, 0), "Invalid SRP public value");
        Bytes u = c_.hash("SHA512", join(a, serverPublic_));
        Bytes v = c_.mod(verifier_, u, n, '^');
        Bytes s = c_.mod(c_.mod(a, v, n, '*'), exponent_, n, '^');
        Bytes key = c_.hash("SHA512", minimal(s));
        Bytes h = c_.hash("SHA512", n), hg = c_.hash("SHA512", g);
        for (size_t i = 0; i < h.size(); ++i)
            h[i] ^= hg[i];
        Bytes expected =
            c_.hash("SHA512",
                    join(join(join(join(join(h, c_.hash("SHA512", bytes("Pair-Setup"))), salt_), a),
                              serverPublic_),
                         key));
        require(equal(expected, proof), "SRP client proof rejected");
        setupKey_ = key;
        setupState_ = 4;
        return join(tlv8(6, Bytes(1, 4)), tlv8(4, c_.hash("SHA512", join(join(a, proof), key))));
    }
    require(state == 5 && setupState_ == 4, "Pair-setup stage order");
    setupState_ = 0;
    Bytes enc = c_.hkdf(setupKey_, "Pair-Setup-Encrypt-Salt", "Pair-Setup-Encrypt-Info");
    std::map<U8, Bytes> sub = parseTlv8(c_.open(enc, nonce("PS-Msg05"), field(t, 5), Bytes()));
    Bytes id = field(sub, 1), pub = field(sub, 3), sig = field(sub, 10);
    require(!id.empty() && id.size() <= 128, "Controller identity length");
    Bytes prefix =
        c_.hkdf(setupKey_, "Pair-Setup-Controller-Sign-Salt", "Pair-Setup-Controller-Sign-Info");
    require(c_.verify(pub, join(join(prefix, id), pub), sig), "Controller key proof rejected");
    require(stored_.dict["peers"].dict.size() < 16 || stored_.dict["peers"].dict.count(text(id)),
            "Receiver pairing store full");
    stored_.dict["peers"].dict[text(id)] = Value::blob(pub);
    store_.write(binaryPlist(stored_));
    Bytes own = bytes(identifier()), ownPub = publicKey();
    prefix = c_.hkdf(setupKey_, "Pair-Setup-Accessory-Sign-Salt", "Pair-Setup-Accessory-Sign-Info");
    Bytes response = join(join(tlv8(1, own), tlv8(3, ownPub)),
                          tlv8(10, c_.sign(seed_, join(join(prefix, own), ownPub))));
    setupKey_.clear();
    return join(tlv8(6, Bytes(1, 6)), tlv8(5, c_.seal(enc, nonce("PS-Msg06"), response, Bytes())));
}
Bytes Pairing::verify(const std::map<U8, Bytes> &t) {
    unsigned state = field(t, 6)[0];
    if (state == 1) {
        verifyState_ = 0;
        readKey.clear();
        writeKey.clear();
        sharedSecret.clear();
        peerEphemeral_ = field(t, 3);
        Bytes privateKey = c_.random(32);
        ephemeral_ = c_.publicKey("X25519", privateKey);
        sharedSecret = c_.shared(privateKey, peerEphemeral_);
        eraseSecret(privateKey);
        verifyKey_ = c_.hkdf(sharedSecret, "Pair-Verify-Encrypt-Salt", "Pair-Verify-Encrypt-Info");
        Bytes id = bytes(identifier());
        Bytes sub =
            join(tlv8(1, id), tlv8(10, c_.sign(seed_, join(join(ephemeral_, id), peerEphemeral_))));
        verifyState_ = 2;
        return join(join(tlv8(6, Bytes(1, 2)), tlv8(3, ephemeral_)),
                    tlv8(5, c_.seal(verifyKey_, nonce("PV-Msg02"), sub, Bytes())));
    }
    require(state == 3 && verifyState_ == 2, "Pair-verify stage order");
    verifyState_ = 0;
    std::map<U8, Bytes> sub =
        parseTlv8(c_.open(verifyKey_, nonce("PV-Msg03"), field(t, 5), Bytes()));
    Bytes id = field(sub, 1);
    const Value &pub = stored_.at("peers").at(text(id));
    require(pub.type == Value::Data &&
                c_.verify(pub.data, join(join(peerEphemeral_, id), ephemeral_), field(sub, 10)),
            "Untrusted controller");
    readKey = c_.hkdf(sharedSecret, "Control-Salt", "Control-Write-Encryption-Key");
    writeKey = c_.hkdf(sharedSecret, "Control-Salt", "Control-Read-Encryption-Key");
    verifyState_ = 4;
    return tlv8(6, Bytes(1, 4));
}
Bytes Pairing::mfiSap(AuthProvider &auth, const Bytes &body) {
    require(!readKey.empty() && body.size() == 33 && body[0] == 1, "Invalid MFiSAP setup");
    Bytes seed = c_.random(32), pub = c_.publicKey("X25519", seed), peer = slice(body, 1, 32),
          shared = c_.shared(seed, peer);
    Bytes key = slice(c_.hash("SHA1", join(bytes("AES-KEY"), shared)), 0, 16),
          iv = slice(c_.hash("SHA1", join(bytes("AES-IV"), shared)), 0, 16);
    Bytes signature = c_.aesCtr(key, iv, auth.signChallenge(c_.hash("SHA256", join(pub, peer))));
    Bytes cert = auth.certificate();
    Bytes out = pub, length(4);
    put32(length, 0, U32(cert.size()));
    out = join(join(out, length), cert);
    put32(length, 0, U32(signature.size()));
    return join(join(out, length), signature);
}
} // namespace cp
