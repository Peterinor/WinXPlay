#pragma once
#include "media_backend.h"
namespace cp {
// Standard WinCE DMO adapter. The device profile supplies only the decoder CLSID.
bool winceAacAvailable();
AudioOutputBackend *createWinceAacOutput(unsigned rate,unsigned channels);
}
