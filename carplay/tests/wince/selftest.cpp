#include "../../platform/common/native_crypto.h"
#include <mmsystem.h>
#include "../../platform/wince/media_backend.h"
using namespace cp;
Value selftest() {
    Value report = Value::dictionary();
    AirCrypto crypto(L"static wolfCrypt");
    require(hex(crypto.hash("SHA256", bytes("abc"))) ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA256 KAT");
    require(hex(crypto.hash("SHA512", bytes("abc"))) ==
                "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836"
                "ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
            "SHA512 KAT");
    Bytes seed = unhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"),
          pub = crypto.publicKey("ED25519", seed);
    require(hex(pub) == "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
            "Ed25519 public RFC8032");
    Bytes sig = crypto.sign(seed, Bytes());
    require(hex(sig) == "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590"
                        "a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
            "Ed25519 signature RFC8032");
    require(crypto.verify(pub, Bytes(), sig), "Ed25519 verify");
    sig[0] ^= 1;
    require(!crypto.verify(pub, Bytes(), sig), "Ed25519 tamper rejection");
    Bytes alice = unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a"),
          bob = unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    require(hex(crypto.publicKey("X25519", alice)) ==
                "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a",
            "X25519 public RFC7748");
    require(hex(crypto.shared(alice, bob)) ==
                "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742",
            "X25519 shared RFC7748");
    Bytes key = crypto.random(32), nonce(12, 0), plain = bytes("WinCE CarPlay authenticated media"),
          aad = bytes("header");
    Bytes sealed = crypto.seal(key, nonce, plain, aad);
    require(crypto.open(key, nonce, sealed, aad) == plain, "AEAD roundtrip");
    sealed[0] ^= 1;
    bool rejected = false;
    try {
        crypto.open(key, nonce, sealed, aad);
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "AEAD tamper rejection");
    require(hex(crypto.aesCtr(unhex("2b7e151628aed2a6abf7158809cf4f3c"),
                              unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"),
                              unhex("6bc1bee22e409f96e93d7e117393172a"))) ==
                "874d6191b620e3261bef6864990db6ce",
            "AES CTR NIST");
    require(crypto.mod(Bytes(1, 4), Bytes(1, 13), Bytes(1, 23), '^') == Bytes(1, 16),
            "Modular exponent");
    Value signedValue = Value::num(U64(-1));
    signedValue.negative = true;
    require(parsePlist(xmlPlist(signedValue)).negative, "Signed plist");
    U8 y[8] = {16, 235, 0, 0, 81, 145, 0, 0}, u[2] = {128, 0}, v[2] = {128, 0};
    DecodedFrame testFrame;
    testFrame.format = PixelYuv420p;
    testFrame.width = 2;
    testFrame.height = 2;
    testFrame.plane[0] = y;
    testFrame.plane[1] = u;
    testFrame.plane[2] = v;
    testFrame.stride[0] = 4;
    testFrame.stride[1] = testFrame.stride[2] = 2;
    Bytes rgb = frameToBgr(testFrame);
    require(rgb.size() == 12 && rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0 && rgb[3] == 255 &&
                rgb[4] == 255 && rgb[5] == 255,
            "YUV black/white and padded stride");
    testFrame.format = PixelNativeSurface;
    bool badFrame = false;
    try {
        frameToBgr(testFrame);
    } catch (const std::exception &) {
        badFrame = true;
    }
    require(badFrame, "Native surface must not be treated as CPU pixels");
    testFrame.format = PixelYuv420p;
    testFrame.stride[0] = 1;
    badFrame = false;
    try {
        frameToBgr(testFrame);
    } catch (const std::exception &) {
        badFrame = true;
    }
    require(badFrame, "Short stride rejected");
    report.dict["frame_layout_tests"] = Value::flag(true);
    AudioOutputBackend *audio = createWaveOutput(48000, 2);
    try {
        audio->feed(Bytes(1920, 0));
        Sleep(20);
        audio->reap();
    } catch (...) {
        delete audio;
        throw;
    }
    delete audio;
    report.dict["wave_pcm_submission"] = Value::flag(true);
    HDC screen = GetDC(0), dc = screen ? CreateCompatibleDC(screen) : 0;
    HBITMAP bitmap = screen ? CreateCompatibleBitmap(screen, 3, 2) : 0;
    if (!dc || !bitmap) {
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        if (screen)
            ReleaseDC(0, screen);
        require(false, "GDI selftest allocation");
    }
    HGDIOBJ prior = SelectObject(dc, bitmap);
    FramePresenter *present = createGdiPresenter();
    Bytes pixels(18, 0);
    pixels[2] = 255;
    pixels[15] = 255;
    DecodedFrame image;
    image.width = 3;
    image.height = 2;
    image.stride[0] = 9;
    image.plane[0] = &pixels[0];
    RECT rectangle = {0, 0, 3, 2};
    present->paint(dc, rectangle, image);
    COLORREF red = GetPixel(dc, 0, 0), blue = GetPixel(dc, 2, 1);
    delete present;
    SelectObject(dc, prior);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(0, screen);
    require(red == RGB(255, 0, 0) && blue == RGB(0, 0, 255), "GDI BGR stride and orientation");
    report.dict["gdi_padded_frame_render"] = Value::flag(true);

    report.dict["crypto_known_answer_tests"] = Value::flag(true);
    report.dict["aead_tamper_rejected"] = Value::flag(true);
    std::wstring store = executableDirectory() + L"\\ce-selftest-store.dat";
    protectedWrite(store, plain);
    require(protectedRead(store) == plain, "Protected store roundtrip");
    DeleteFileW((store + L".bak").c_str());
    require(MoveFileW(store.c_str(), (store + L".bak").c_str()) != FALSE,
            "Simulate interrupted store replacement");
    require(protectedRead(store) == plain, "Protected store recovery");
    DeleteFileW(store.c_str());
    report.dict["protected_store_recovery"] = Value::flag(true);
    report.dict["passed"] = Value::flag(true);
    report.dict["physical_carplay_verified"] = Value::flag(false);
    OSVERSIONINFO os = {sizeof(os)};
    GetVersionEx(&os);
    report.dict["os_major"] = Value::num(os.dwMajorVersion);
    report.dict["os_minor"] = Value::num(os.dwMinorVersion);
    SYSTEM_INFO cpu;
    GetSystemInfo(&cpu);
    report.dict["processor_architecture"] = Value::num(cpu.wProcessorArchitecture);
    report.dict["wave_out_devices"] = Value::num(waveOutGetNumDevs());
    report.dict["wave_in_devices"] = Value::num(waveInGetNumDevs());
    return report;
}
#ifdef UNDER_CE
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
#endif
    Value report;
    try {
        report = selftest();
    } catch (const std::exception &e) {
        report = Value::dictionary();
        report.dict["error"] = Value::str(e.what());
        report.dict["passed"] = Value::flag(false);
    }
    try {
        writeFile(executableDirectory() + L"/ce-selftest.plist", xmlPlist(report));
    } catch (...) {
        return 2;
    }
    return report.at("passed").boolean ? 0 : 1;
}
