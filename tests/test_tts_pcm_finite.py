"""Execute the AV/sync shared finite PCM engine as ARM; substitute OS/hardware."""
import struct
import unittest
from test_voice_pcm import VoiceRun


class FiniteRun(VoiceRun):
    def __init__(self, failure=None):
        super().__init__(failure)
        self.audio_blocks=[]

    def hook(self,u,address,size,data):
        if self.hooks.get(address)=='waveOutWrite':
            from unicorn.arm_const import UC_ARM_REG_R1
            h=u.reg_read(UC_ARM_REG_R1)
            self.audio_blocks.append(bytes(u.mem_read(self.get(h),self.get(h+4))))
        super().hook(u,address,size,data)

    def run(self, seconds=6, sync=0):
        self.result=self.arm.call('TtsPcmContract',seconds,sync,instruction_limit=100000000)
        return self


class FiniteTests(unittest.TestCase):
    def test_distinct_counts_two_rounds_and_exact_finite_length(self):
        m=FiniteRun().run(seconds=20)
        self.assertEqual(m.result,0)
        self.assertEqual(m.requests(),[0x33,0x34])
        self.assertEqual(len(m.audio_blocks),200)
        self.assertEqual(m.played,960000)
        self.assertEqual(len(set(m.initial)),10)
        samples=struct.unpack('<1920000h',b''.join(m.audio_blocks))
        left,right=samples[::2],samples[1::2]
        def pulse_windows(channel):
            active=[]
            # 10ms energy windows ignore zero crossings inside each tone.
            for i in range(0,len(channel),480):
                if any(channel[i:i+480]):active.append(i//480)
            starts=[n for n in active if n-1 not in active]
            return [n/100 for n in starts],active
        starts_l,active_l=pulse_windows(left);starts_r,active_r=pulse_windows(right)
        self.assertEqual(starts_l,[2,5,5.5,11,14,14.5])
        self.assertEqual(starts_r,[2,8,8.5,9,11,17,17.5,18])
        expected_l=[int(t*100)+i for t in starts_l for i in range(12)]
        expected_r=[int(t*100)+i for t in starts_r for i in range(12)]
        self.assertEqual(active_l,expected_l);self.assertEqual(active_r,expected_r)
        self.assertEqual(left[2*48000:3*48000],right[2*48000:3*48000])
        self.assertFalse(m.pending);self.assertFalse(m.prepared)
        names=[n for n,a in m.calls]
        self.assertNotIn('waveOutPause',names);self.assertNotIn('waveOutRestart',names)
        writes=[i for i,n in enumerate(names) if n=='waveOutWrite']
        self.assertEqual(writes[9]-writes[0],9)

    def test_sync_beep_matches_flash_timeline(self):
        m=FiniteRun().run(sync=1)
        self.assertEqual(m.result,0)
        samples=struct.unpack('<576000h',b''.join(m.audio_blocks))
        self.assertEqual(samples[::2],samples[1::2])
        mono=samples[::2]
        for second in range(6):
            self.assertTrue(any(mono[second*48000:second*48000+5760]))
            self.assertFalse(any(mono[second*48000+5760:(second+1)*48000]))

    def test_failures_release_acquired_channel(self):
        for failure in ('wave-open','prepare','write','refill','stall','release'):
            with self.subTest(failure=failure):
                m=FiniteRun(failure).run(seconds=2)
                self.assertNotEqual(m.result,0)
                self.assertEqual(m.requests(),[0x33,0x34])
                self.assertFalse(m.pending);self.assertFalse(m.prepared)

    def test_invalid_or_failed_acquisition_never_queues(self):
        for failure in ('missing-device','av-open','request'):
            m=FiniteRun(failure).run()
            self.assertNotEqual(m.result,0)
            self.assertFalse(m.audio_blocks)
            self.assertNotIn(0x34,m.requests())
        for seconds in (0,61):
            m=FiniteRun().run(seconds=seconds)
            self.assertEqual(m.result,18)
            self.assertTrue(all(name in ('memset','GetTickCount') for name,args in m.calls))


if __name__=='__main__':unittest.main()
