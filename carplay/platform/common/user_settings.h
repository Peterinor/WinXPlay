#ifndef MRN_CE_WOLF_SETTINGS
#define MRN_CE_WOLF_SETTINGS
#include <stdlib.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#define WOLFSSL_USER_IO
#define NO_FILESYSTEM
#define NO_WRITEV
#define NO_DEV_RANDOM
#define WOLFSSL_NO_ASM
#define USE_FAST_MATH
#define TFM_NO_ASM
#define TFM_TIMING_RESISTANT
#define ECC_TIMING_RESISTANT
#define WC_RSA_BLINDING
#define SIZEOF_LONG_LONG 8
#define FP_MAX_BITS 8192
#define WOLFSSL_SHA512
#define HAVE_HKDF
#define HAVE_CHACHA
#define HAVE_POLY1305
#define HAVE_ONE_TIME_AUTH
#define HAVE_ED25519
#define HAVE_CURVE25519
#define CURVE25519_SMALL
#define ED25519_SMALL
#define WOLFSSL_AES_COUNTER
#define HAVE_AESGCM
#define HAVE_ECC
#define HAVE_TLS_EXTENSIONS
#define HAVE_SUPPORTED_CURVES
#define HAVE_EXTENDED_MASTER
#define NO_DSA
#define NO_RC4
#define NO_DES3
#define NO_OLD_TLS
#define WOLFSSL_NO_INTRINSICS
#define WOLFSSL_NO_ATOMICS
#define XVSNPRINTF _vsnprintf
#define WOLFSSL_DH_CONST
#define WOLFSSL_CERT_GEN
// Lockdown's pairing profile (DiPlay/libimobiledevice) uses serial number 0.
// Accept that specific encoding; retain strict ASN parsing and signature checks.
#define WOLFSSL_ASN_ALLOW_0_SERIAL
#define WOLFSSL_CERT_EXT
#define USE_WOLF_STRTOK
#define WOLFSSL_KEY_GEN
#define WOLFSSL_ALT_NAMES
#define KEEP_PEER_CERT
#define NO_INLINE
#define CUSTOM_RAND_GENERATE_SEED mrn_random_seed
#ifdef __cplusplus
extern "C" {
#endif
int mrn_random_seed(unsigned char *output, unsigned int size);
#ifdef __cplusplus
}
#endif
#endif
