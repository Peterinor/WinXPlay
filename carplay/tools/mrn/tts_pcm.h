// Finite device-1/TTS playback shared by the AV audio and sync modes.
#pragma once
#include <windows.h>
#include <mmsystem.h>

class TtsPcm {
    enum { Frames=4800, Blocks=10, Rate=48000 };
    WAVEHDR headers_[Blocks];
    short samples_[Blocks][Frames*2];
    BOOL prepared_[Blocks], queued_[Blocks], sync_;
    DWORD total_, generated_, next_;
    void fill(DWORD block) {
        for(DWORD i=0;i<Frames;++i) {
            DWORD frame=generated_+i,phase=frame%Rate,channel=0;
            BOOL beep=phase<5760;
            // Sync retains a 120ms beep at each second boundary.
            // Audio: 2s silence, then two 9s rounds: BOTH x1 / LEFT x2 / RIGHT x3.
            // Each group lasts 3s; short tones start 0.5s apart within its group.
            if(!sync_) {
                beep=FALSE;
                if(frame>=2*Rate&&frame<20*Rate) {
                    DWORD within=(frame-2*Rate)%(9*Rate);
                    channel=within/(3*Rate);
                    DWORD groupFrame=within%(3*Rate),pulse=groupFrame/(Rate/2);
                    phase=groupFrame%(Rate/2);
                    beep=pulse<channel+1&&phase<5760;
                }
            }
            LONG value=0;
            if(beep) {
                DWORD p=frame%80;value=p<40?(LONG)p*60-1200:3600-(LONG)p*60;
                DWORD ramp=phase<240?phase:5759-phase<240?5759-phase:240;
                value=value*(LONG)ramp/240;
            }
            samples_[block][i*2]=(short)((sync_||channel!=2)?value:0);
            samples_[block][i*2+1]=(short)((sync_||channel!=1)?value:0);
        }
        generated_+=Frames;
    }
    DWORD write(DWORD block) {
        DWORD r=waveOutWrite(wave,&headers_[block],sizeof(WAVEHDR));
        if(r){error=r;return r;}
        queued_[block]=TRUE;++submitted;return 0;
    }
  public:
    HANDLE av;HWAVEOUT wave;
    BOOL requested, clean;
    DWORD submitted,completed,error,requestError,releaseError,resetError,unprepareError,closeError,startTick;
    TtsPcm():av(INVALID_HANDLE_VALUE),wave(0),requested(FALSE),clean(TRUE),submitted(0),completed(0),error(0),
        requestError(0),releaseError(0),resetError(0),unprepareError(0),closeError(0),startTick(0) {
        ZeroMemory(headers_,sizeof(headers_));ZeroMemory(prepared_,sizeof(prepared_));ZeroMemory(queued_,sizeof(queued_));
    }
    DWORD open(DWORD seconds,BOOL sync) {
        if(!seconds||seconds>60||wave||requested||av!=INVALID_HANDLE_VALUE||!clean)return 18;
        total_=seconds*Rate;generated_=next_=submitted=completed=error=0;sync_=sync;
        if(waveOutGetNumDevs()<=1)return 19;
        av=CreateFileW(L"AVV0:",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);
        if(av==INVALID_HANDLE_VALUE){requestError=GetLastError();return 20;}
        BYTE source=4;
        requested=DeviceIoControl(av,0x33,&source,1,0,0,0,0);
        if(!requested){requestError=GetLastError();return 21;}
        Sleep(2000); // settling only; not a connection acknowledgement
        WAVEFORMATEX f={0};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=2;f.nSamplesPerSec=Rate;
        f.wBitsPerSample=16;f.nBlockAlign=4;f.nAvgBytesPerSec=Rate*4;
        error=waveOutOpen(&wave,1,&f,0,0,CALLBACK_NULL);
        if(error){wave=0;return 22;}
        for(DWORD i=0;i<Blocks;++i) {
            ZeroMemory(&headers_[i],sizeof(WAVEHDR));fill(i);
            headers_[i].lpData=(LPSTR)samples_[i];headers_[i].dwBufferLength=Frames*4;
            error=waveOutPrepareHeader(wave,&headers_[i],sizeof(WAVEHDR));
            if(error)return 23;prepared_[i]=TRUE;
        }
        return 0;
    }
    DWORD start() {
        if(!wave||submitted||error)return 24;
        startTick=GetTickCount();
        // At least six immediate submissions are required by the OEM SSI driver.
        for(DWORD i=0;i<Blocks;++i)if(write(i))return 25;
        return 0;
    }
    DWORD pump() {
        if(error)return error;
        for(DWORD n=0;n<Blocks;++n) {
            if(!queued_[next_]||!(headers_[next_].dwFlags&WHDR_DONE))break;
            queued_[next_]=FALSE;++completed;
            if(generated_<total_){fill(next_);if(write(next_))return error;}
            next_=(next_+1)%Blocks;
        }
        return 0;
    }
    BOOL done() const {return wave&&submitted&&completed*Frames==total_&&!error;}
    BOOL close() {
        if(wave) {
            resetError=waveOutReset(wave);
            for(DWORD i=0;i<Blocks;++i)if(prepared_[i]) {
                DWORD r=waveOutUnprepareHeader(wave,&headers_[i],sizeof(WAVEHDR));
                if(r)unprepareError=r;else prepared_[i]=FALSE;
            }
            closeError=waveOutClose(wave);
            if(!closeError)wave=0;
        }
        if(requested){BYTE source=4;if(!DeviceIoControl(av,0x34,&source,1,0,0,0,0))releaseError=GetLastError()?GetLastError():1;requested=FALSE;}
        if(av!=INVALID_HANDLE_VALUE){if(!CloseHandle(av))releaseError=GetLastError()?GetLastError():1;av=INVALID_HANDLE_VALUE;}
        clean=!resetError&&!unprepareError&&!closeError&&!releaseError;
        return clean;
    }
};
