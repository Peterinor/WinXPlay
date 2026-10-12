# MRN 音频输出与采集

适用范围见 [MRN 设备说明](../mrn.md)。原厂接口结论来自 ENTAVMAIN、ENTSSIAUDIO、ENTSSIHFPAUDIO、ENTDICTAPHONEMAIN 及导航包装器的指令和注册配置核对。下文区分原厂契约、当前实现及实听范围，不以 API 成功替代出声或人声验收。

## 输出设备与音源是两层接口

| 对象 | 已确认含义 |
| --- | --- |
| waveOut 设备 0，`Multi-channel Audio Output` | 原厂主媒体 ESAI 输出候选；完成 PCM 提交不保证扬声器可听 |
| waveOut 设备 1，`Audio Output` | 原厂语音合成选择的输出设备；当前 MRN PCM 后端采用它 |
| `entSSIAudio.dll` / 注册流设备 `WAV2:` | SSI 导航/语音驱动；流设备后缀不等于 waveOut 枚举编号 |
| `AVV0:` | 原厂音源协调接口，不直接接收 PCM 缓冲 |

枚举编号只对已验证的设备配置成立。打开设备、缓冲完成、驱动位置递增、原厂音源申请及扬声器出声是不同阶段。

## 当前采用的 TTS 输出契约

| 顺序 | 接口与行为 |
| --- | --- |
| 申请 | 打开 `AVV0:`，发送 IOCTL `0x33`，输入单字节 `0x04` |
| 等待 | 可取消地等待 2000 ms；这是稳定等待，不是异步激活确认 |
| 播放 | waveOut 设备 1，PCM16、48000 Hz、双声道；十个独立缓冲持续补充 |
| 清理 | 停止提交，重置并取消准备音频缓冲，关闭 waveOut |
| 释放 | 向 `AVV0:` 发送 IOCTL `0x34`，输入单字节 `0x04`，关闭句柄 |

`0x04` 是该语音接口的源标识，不是输出设备号。原厂文本合成请求也在 AVV0 上，但不能把 PCM 当文本发送。当前路径保留原厂 AV 服务，不通过结束原厂进程获得音源。

SSI 驱动的启动条件是第六次 WODM_WRITE 才触发 Run；计数对象是提交次数，而非音频字节数或时长。一次大缓冲加 Pause/Restart 不能替代多次提交；该计数还属于驱动全局状态，不能推广成所有 waveOut 驱动的规则。

当前 [MRN 输出实现](../../../carplay/devices/mrn/audio_output.cpp)使用十个独立 WAVEHDR，每个 4800 帧（48 kHz 下 100 ms），不使用 Pause/Restart 启动。多个逻辑流共用一个输出流，避免重复打开 SSI。每个逻辑流通过[有界 PCM 队列](../../../carplay/devices/mrn/audio_pcm.h)转换为 48 kHz 双声道；队列满时丢弃过期帧，混合结果做整数限幅。若驱动未释放缓冲，保留内存与错误状态，不释放仍可能被驱动访问的存储。

## 实听结论与未解决的仲裁

设备 1、TTS 申请、多缓冲的组合已有指定 MRN 上可听 PCM 及与原音源并行出声的记录，支持当前后端选择。它不证明任意源组合均能混音，也未确定混音发生的硬件环节。

完整音乐播放仍需验证原音源静音/保留、左右声道、长期稳定性、音量恢复和通话优先级。释放请求成功不等于原音源或音量已恢复。`physical_audio_verified`、`source_connection_verified` 保持为 false 的诊断字段不能擅自改成物理成功。

原厂 USB 文件媒体源的业务编号 `0x0A` 映射连接管理源 `0x0E`，并涉及可用性、预激活、激活和暂停/恢复回应。发一次“选择 USB”不会自动把独立 waveOut 流接入原厂播放器。该路线没有作为当前接收端输出实现，不应替换上述 TTS 契约。

## 麦克风：已知接口与实验边界

原厂语音识别采用 PCM16、16 kHz、单声道；录音机业务采用 PCM16、22050 Hz、单声道。ENTSSIHFPAUDIO 中两种采样率进入不同 DMA 启动分支，其他采样率的格式查询成功不代表能从空闲状态开始采集。录音机的 22.05 kHz 业务格式不能因驱动内部状态出现 44.1 kHz 就自行改写。

当前独立录音测试复刻的请求顺序为：

| 阶段 | AVV0 IOCTL | 输入字节（十六进制） |
| --- | --- | --- |
| 标记录音机源可用 | `4` | `10 01` |
| 选择 | `1` | `10` |
| 标记不可用 | `4` | `10 00` |
| 取消选择 | `2` | `10` |

请求由[录音源适配](../../../carplay/tools/mrn/microphone_route.h)实现，操作步骤见[诊断工具](../../../carplay/tools/mrn/README.md)。测试选 waveIn 设备 0，不把 waveOut 设备 1 或 `WAV3:` 后缀作为输入编号；waveIn 0 到具体流驱动的运行时映射尚未直接确认。

录音源实验没有完整的异步激活确认或原会话归属查询，只能在原厂录音、语音识别、电话及接收端采集空闲时运行。有效人声、释放后恢复、CarPlay 上行、Siri 和全双工回声消除均未由该接口分析证明；非零样本也可能只是噪声。

## 维护和回归入口

输出后端选择位于 [MRN 装配](../../../carplay/devices/mrn/wince.cpp)，退避重开策略位于 [audio_recovery.cpp](../../../carplay/devices/mrn/audio_recovery.cpp)。平台注册入口只委托设备选择，Generic 使用标准 wave，不启用 MRN TTS。[MRN 输出测试](../../../carplay/tests/wince/mrn_audio_test.cpp)、[PCM 测试](../../../tests/test_voice_pcm.py)及[有限播放检查](../../../tests/test_tts_pcm_finite.py)覆盖相应软件行为。上述回归不提供真实音源仲裁或声学测量；物理能力以[兼容性表](../../compatibility.md)为准。
