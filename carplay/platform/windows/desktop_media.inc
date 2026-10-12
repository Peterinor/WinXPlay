void playPcm16(const Bytes &pcm, unsigned rate, unsigned channels) {
    require(!pcm.empty() && channels >= 1 && channels <= 2 && rate >= 8000 && rate <= 48000 &&
                pcm.size() % (channels * 2) == 0 && pcm.size() <= rate * channels * 2 * 10,
            "Invalid PCM format");
    WAVEFORMATEX f;
    ZeroMemory(&f, sizeof(f));
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = WORD(channels);
    f.nSamplesPerSec = rate;
    f.wBitsPerSample = 16;
    f.nBlockAlign = WORD(channels * 2);
    f.nAvgBytesPerSec = rate * f.nBlockAlign;
    HWAVEOUT wave = 0;
    require(waveOutOpen(&wave, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR,
            "Cannot open Windows audio output");
    WAVEHDR h;
    ZeroMemory(&h, sizeof(h));
    h.lpData = (LPSTR)&pcm[0];
    h.dwBufferLength = DWORD(pcm.size());
    bool prepared = false;
    try {
        require(waveOutPrepareHeader(wave, &h, sizeof(h)) == 0, "Cannot prepare audio");
        prepared = true;
        require(waveOutWrite(wave, &h, sizeof(h)) == 0, "Cannot submit audio");
        DWORD start = GetTickCount();
        while (!(h.dwFlags & WHDR_DONE)) {
            require(DWORD(GetTickCount() - start) < 12000, "Audio playback timeout");
            Sleep(2);
        }
        waveOutUnprepareHeader(wave, &h, sizeof(h));
        waveOutClose(wave);
    } catch (...) {
        waveOutReset(wave);
        if (prepared)
            waveOutUnprepareHeader(wave, &h, sizeof(h));
        waveOutClose(wave);
        throw;
    }
}
