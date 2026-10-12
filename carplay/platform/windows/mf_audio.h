#ifndef CP_MF_AUDIO_H
#define CP_MF_AUDIO_H
#include <windows.h>
// One complete raw AAC-LC access unit -> interleaved signed 16-bit PCM.
// COM/MF objects live on a private worker; callers receive PCM synchronously.
extern "C" {
HRESULT cpAacCreate(unsigned rate,unsigned channels,void **handle);
HRESULT cpAacDecode(void *handle,const unsigned char *data,unsigned size,
                    unsigned char *pcm,unsigned capacity,unsigned *written);
HRESULT cpAacDrain(void *handle,unsigned char *pcm,unsigned capacity,unsigned *written);
void cpAacDestroy(void *handle);
}
#endif
