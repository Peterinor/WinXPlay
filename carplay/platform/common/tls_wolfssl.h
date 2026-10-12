// Included inside the shared Lockdown implementation's anonymous namespace.
class Channel {
    SOCKET socket_;
    WOLFSSL_CTX *context_;
    WOLFSSL *ssl_;
    bool pinned_, initialized_;
    void ioFailure(const char *op,int result,size_t done,size_t wanted){
        int error=result<0?WSAGetLastError():0;
        int tls=ssl_?wolfSSL_get_error(ssl_,result):0;
        std::ostringstream s;s<<op<<" result="<<result<<" wsa="<<error<<" tls="<<tls
            <<" transferred="<<done<<" expected="<<wanted;
        diagnosticEvent("LOCKDOWN_IO_FAILED",s.str());throw std::runtime_error(s.str());
    }
    Channel(const Channel &);
    Channel &operator=(const Channel &);
    static int receive(WOLFSSL *, char *out, int n, void *context) {
        SOCKET s = *(SOCKET *)context;
        int got = recv(s, out, n, 0);
        if (got > 0)
            return got;
        if (!got)
            return WOLFSSL_CBIO_ERR_CONN_CLOSE;
        int e = WSAGetLastError();
        return e == WSAETIMEDOUT || e == WSAEWOULDBLOCK ? WOLFSSL_CBIO_ERR_WANT_READ
                                                        : WOLFSSL_CBIO_ERR_GENERAL;
    }
    static int sendBytes(WOLFSSL *, char *data, int n, void *context) {
        int count = send(*(SOCKET *)context, data, n, 0);
        return count > 0 ? count : WOLFSSL_CBIO_ERR_GENERAL;
    }
    Bytes read(size_t n) {
        require(n <= 4 * 1024 * 1024, "Lockdown length limit");
        Bytes b(n);
        size_t offset = 0;
        while (offset < n) {
            require(!ssl_ || pinned_, "TLS peer not pinned");
            int got = ssl_ ? wolfSSL_read(ssl_, &b[offset], n - offset)
                           : recv(socket_, (char *)&b[offset], n - offset, 0);
            if(got<=0)ioFailure("Lockdown read",got,offset,n);
            offset += got;
        }
        return b;
    }
    static Bytes publicKey(WOLFSSL_X509 *cert) {
        require(cert != 0, "Pairing certificate unavailable");
        Bytes out(4096);
        int size = out.size();
        require(wolfSSL_X509_get_pubkey_buffer(cert, &out[0], &size) == WOLFSSL_SUCCESS &&
                    size > 0 && size <= 4096,
                "Pairing public key unavailable");
        out.resize(size);
        return out;
    }

  public:
    explicit Channel(SOCKET s)
        : socket_(s), context_(0), ssl_(0), pinned_(false), initialized_(false) {}
    ~Channel() {
        if (ssl_)
            wolfSSL_free(ssl_);
        if (context_)
            wolfSSL_CTX_free(context_);
        if (initialized_)
            wolfSSL_Cleanup();
        closesocket(socket_);
    }
    void sendBytes(const Bytes &b) {
        require(!ssl_ || pinned_, "TLS peer not pinned");
        size_t offset = 0;
        while (offset < b.size()) {
            int n = ssl_ ? wolfSSL_write(ssl_, &b[offset], b.size() - offset)
                         : send(socket_, (const char *)&b[offset], b.size() - offset, 0);
            if(n<=0)ioFailure("Lockdown write",n,offset,b.size());
            offset += n;
        }
    }
    Bytes receiveSome() {
        require(!ssl_ || pinned_, "TLS peer not pinned");
        DWORD timeout = 500;
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
        Bytes b(4096);
        int got =
            ssl_ ? wolfSSL_read(ssl_, &b[0], b.size()) : recv(socket_, (char *)&b[0], b.size(), 0);
        if (got < 0) {
            int e = ssl_ ? wolfSSL_get_error(ssl_, got) : 0;
            if (e == WOLFSSL_ERROR_WANT_READ || WSAGetLastError() == WSAETIMEDOUT ||
                WSAGetLastError() == WSAEWOULDBLOCK)
                return Bytes();
        }
        require(got > 0, "CarKit stream closed");
        b.resize(got);
        return b;
    }
    void secure(const Value &record) {
        diagnosticEvent("TLS_BEGIN");
        require(!ssl_, "TLS already active");
        require(wolfSSL_Init() == WOLFSSL_SUCCESS, "TLS initialization");
        initialized_ = true;
        context_ = wolfSSL_CTX_new(wolfTLSv1_2_client_method());
        require(context_ != 0, "TLS context");
        const Bytes &certificate = record.at("RootCertificate").data;
        const Bytes &key = record.at("RootPrivateKey").data;
        require(!certificate.empty() && !key.empty(), "Missing trusted pair record");
        require(wolfSSL_CTX_use_certificate_buffer(context_, &certificate[0], certificate.size(),
                                                   WOLFSSL_FILETYPE_PEM) == WOLFSSL_SUCCESS,
                "TLS local certificate");
        require(wolfSSL_CTX_use_PrivateKey_buffer(context_, &key[0], key.size(),
                                                  WOLFSSL_FILETYPE_PEM) == WOLFSSL_SUCCESS,
                "TLS local key");
        // Pairing uses device-key pinning, not the public Web PKI. No application bytes before pin.
        wolfSSL_CTX_set_verify(context_, WOLFSSL_VERIFY_NONE, 0);
        wolfSSL_SetIORecv(context_, receive);
        wolfSSL_SetIOSend(context_, sendBytes);
        ssl_ = wolfSSL_new(context_);
        require(ssl_ != 0, "TLS allocation");
        wolfSSL_SetIOReadCtx(ssl_, &socket_);
        wolfSSL_SetIOWriteCtx(ssl_, &socket_);
        int connected=wolfSSL_connect(ssl_);
        if(connected!=WOLFSSL_SUCCESS)ioFailure("iPhone TLS handshake",connected,0,0);
        const Bytes &expectedPem = record.at("DeviceCertificate").data;
        WOLFSSL_X509 *expected =
            expectedPem.empty() ? 0
                                : wolfSSL_X509_load_certificate_buffer(
                                      &expectedPem[0], expectedPem.size(), WOLFSSL_FILETYPE_PEM);
        WOLFSSL_X509 *actual = wolfSSL_get_peer_certificate(ssl_);
        try {
            pinned_ = publicKey(expected) == publicKey(actual);
        } catch (...) {
            if (expected)
                wolfSSL_X509_free(expected);
            if (actual)
                wolfSSL_X509_free(actual);
            throw;
        }
        wolfSSL_X509_free(expected);
        wolfSSL_X509_free(actual);
        require(pinned_, "iPhone TLS pairing public key mismatch");
        diagnosticEvent("TLS_OK","peer_key_pinned=true");
    }
    Value request(const Value &v) {
        diagnosticEvent("LOCKDOWN_REQUEST",v.at("Request").string);
        Bytes body = xmlPlist(v), head(4);
        put32(head, 0, body.size());
        head.insert(head.end(), body.begin(), body.end());
        sendBytes(head);
        U32 n = be32(read(4), 0);
        require(n > 0 && n <= 4 * 1024 * 1024, "Lockdown response length");
        Value result = parsePlist(read(n));
        diagnosticEvent("LOCKDOWN_RESPONSE",v.at("Request").string+" "+
            (result.dict.count("Error")?result.at("Error").string:"no_error"));
        if (result.dict.count("Error"))
            throw std::runtime_error("Lockdown: " + result.at("Error").string);
        return result;
    }
};
