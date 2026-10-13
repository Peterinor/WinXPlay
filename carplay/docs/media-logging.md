# 正常 CarPlay 播放日志

这是正常 WinXPlay 播放程序，只增加统计。不需要切换测试模式，不保存音视频内容，也不读取 `video-test.txt`。旧的 full / receive / decrypt 三轮方案已取消。

## 使用

1. MRN 当前配套更新包在 `build/packages/winxplay_bridge_update`。按原有启动方式，选择 AutoLaunch 或 AUTOLAUNCH_PROD 一套复制到 U 盘根目录，更新已有安装的 `WinXPlay.exe` 和 `WinXPlayBridge.dll`，沿用现有身份、驱动和配置。DLL 本次开机尚未随 BAND 加载时可先更新再进入；已经加载则需退出持有它的原厂 HMI，或更新后重新启动以加载新版本。
2. 拔下手机，完全退出 WinXPlay。只按 BAND 返回原车界面不代表退出进程。触发默认 `fileops.txt` 安装，确认 `ALL_JOBS_COMPLETE`；保留本次 FILEOPS 日志中的 `BACKUP_PATH`，用于需要时退回旧 EXE。
3. 从已有入口启动内部 WinXPlay，或将 `run-fileops.txt` 内容替换为 `fileops.txt` 后触发。拔 U 盘、接 iPhone，照常使用 CarPlay。
4. 停车时操作约 1～2 分钟：先停留在静止页面，再重复左右翻页、滚动等容易卡顿的操作。记下卡顿大概发生在连接后的第几秒。只需一次正常播放，无需盲操作或专用测速程序。
5. 拔下手机结束会话，插回 U 盘，将 `collect-fileops.txt` 内容替换为 `fileops.txt` 后触发。把整个 FILEOPS_编号 目录带回电脑。未产生音频时，对应音频文件可能不存在；没有发生日志轮换时，previous 文件也可能不存在。

程序目录中的 `connection-trace.log` 和 `connection-trace.previous.log` 保存时间序列；`streaming-diagnostics.plist` 中的 `media_metrics` 是最后一个采样窗口。另带回 `video-backend.plist`、`audio-output.plist`、`audio-recovery.plist` 作为辅助。使用已有轮换上限：当前日志约 512 KiB，上一份约 512 KiB；请在复现后及时导出，避免旧记录被轮换。新会话会覆盖 plist，分析时用 trace 的 pid、session、tick 对齐。

## 每秒统计含义

接收线程大约每秒汇总一次，断连时再汇总一次；报告副本交给标准平台后台线程写盘。`window_ms` 是真实采样间隔，不能假设总是 1000。trace 行首是写盘时刻，分析应使用 `session`、`tick` 对齐采样时刻。所有计数仅针对这一接收器会话，不会因每秒汇总而清零。

后台最多保留 4 份待写报告，磁盘持续堵塞时丢弃最旧待写报告，保留最新状态；不会等待写盘来拖住接收线程。`reports_dropped`、`reports_failed` 是该报告线程累计丢弃/写入异常次数，有增量时不可假设时间序列连续。正常停止会等待最后报告完成。音频辅助 plist 只在启动、故障、关闭等状态转换时保存，逐秒状态以 MEDIA_AUDIO 为准。

DirectShow 的 `video-backend.plist` 和周期 VIDEO_GRAPH 也由后台写入，渲染窗口线程只采样并入队；该报告里的丢弃/失败计数属于视频报告线程，独立于接收报告。`uptime_ms` 为视频采样时刻，完成关闭时等待最终报告写入。

同一 `session`、`tick` 的 MEDIA_RX、MEDIA_VIDEO、MEDIA_AUDIO 对应同次采样。各后台在各自锁内读取，采样不是跨线程的原子快照；小幅差值可能来自在途数据。

| 字段 | 含义 |
|---|---|
| `max_fps` | 本设备配置的帧率上限；不是实测帧率。配置来源和修改方式见 [接收端配置](configuration.md) |
| MEDIA_RX `n1` | unknown；车机无法可靠获知 iPhone 实际发送帧数 |
| `rx_bytes`、`rx_bytes_delta`、`recv_calls` | 视频 TCP 应用层累计字节、本窗口字节、累计读取次数；一次读取不等于一帧 |
| `n2`、`n2_delta` | 收齐并开始处理的 type 0 加密视频消息累计数、本窗口增量；不含配置消息。解析批次在错误处中断时，后续尚未处理的消息不计入 N2 |
| `n3`、`n3_delta` | 成功鉴权、解密并转换成 Annex-B 的 type 0 视频消息累计数、本窗口增量；不是逐 NAL 数，也不保证每个消息恰好包含一个可显示画面 |
| `config`、`parse_errors`、`pending_bytes` | 配置消息累计数、解析失败批次数、解析器待组包字节 |
| `submitted`、`submitted_delta` | 成功调用播放提交入口的 type 0 消息数、增量；播放队列可能忽略首个关键帧之前的数据 |
| `n4_decode`、`n4_display` | unknown；当前 OEM 解码器没有可靠的逐帧解码完成回调，原车渲染统计尚未验证，不能以 0 或源端样本数代替实际显示帧率 |
| `parse_ms`、`parse_max_ms` | 本窗口解析、鉴权、解密、Annex-B 转换的批处理总耗时、最大批处理耗时 |
| `submit_ms`、`submit_max_ms` | 本窗口提交批次的总耗时、最大耗时，包括队列背压等待和该批次的少量接收状态处理；不等于 VPU 解码耗时 |
| `rx_gap_max_ms`、`frame_gap_max_ms` | 本窗口观察到的读取间隔、完整视频消息处理批次间隔的最大值；同一批次内无法还原每帧在 USB 上的到达时刻 |
| `rx_idle_ms`、`frame_idle_ms` | 距上次读取/完整消息处理的时间；有过数据之后，持续无数据时也可观察停顿 |
| `report_ms` | 本窗口内接收线程汇总、复制报告及入队的总墙钟耗时，通常包含上一条报告的入队；后台写盘不计入，旧版同步写盘计入 |
| `audio_process_ms`、`audio_process_max_ms` | 本窗口普通 UDP 音频鉴权、解密、字节序转换及提交音频队列的总耗时、单包最大耗时；包括等待队列锁和线程抢占，不含 buffered AAC、麦克风上传或设备播放时间 |
| `audio_decrypt_ms`、`audio_decrypt_max_ms` | 上述 UDP 音频处理中鉴权解密部分的总耗时、单包最大耗时；是 audio_process 的子集，不能相加 |
| MEDIA_VIDEO `supplied` | 累计填入 DirectShow 源样本的数量，不能认作解码完成或实际显示数量 |
| `enqueued`、`nonqueued` | 实际入视频队列的累计样本数、未入队的累计输入数；后者包含正常配置消息和首个关键帧前忽略的数据，不能直接叫丢帧 |
| `q_frames`、`q_bytes`、`q_peak` | 当前视频队列帧数、字节数、采样间隔内最高帧数 |
| `q_age_ms`、`q_age_max_ms` | 当前最老排队样本的年龄、本窗口已出队样本的最大排队时间 |
| `wait_ms_total`、`wait_max_ms` | 因视频队列满而等待的累计耗时、本窗口最大等待耗时 |
| `supply_gap_max_ms`、`supply_idle_ms` | 源端提供样本的最大间隔、距上次提供样本的时间；不能独立证明渲染器卡顿 |
| MEDIA_AUDIO `q_ms`、`q_peak_ms` | 单路音频软件队列当前时长、本窗口峰值，按重采样后的 48 kHz 内容计算 |
| `device_content_ms` | 已成功提交给音频设备、尚未观察到完成通知的该路内容时长；不含静音填充，不是精确驱动播放位置或端到端延迟 |
| `dropped_frames`、`completed_frames` | 当前音频设备实例累计的软件队列丢弃帧、已收到完成通知的内容帧，均为 48 kHz PCM 帧 |
| `empty_frames` | 当前音频流累计因队列无数据而填充的静音帧，含初始预填充，不含主动暂停；观察增量，不能直接当作用户听到的断音时长 |
| `refill_gap_max_ms` | 当前共享音频设备实例累计最大的补充缓冲间隔，包含线程调度延迟；多路流共用该值，不随采样清零 |
| `feed_lock_ms_total`、`feed_copy_ms_total` | 本音频流累计等待软件队列锁、持锁转换并加入 PCM 队列的耗时；差分后与接收线程音频处理耗时对照，包含线程抢占 |
| `fill_ms_total`、`fill_max_ms` | 当前共享音频设备累计填充缓冲的耗时、单次最大值；单路使用整块复制，多路才执行逐采样混音；包含线程抢占 |
| `write_ms_total`、`write_max_ms` | 当前共享音频设备累计 waveOutWrite 调用耗时、单次最大值；与接收线程分开，不能视为硬件实际播放时间 |
| `device_open`、`failures`、`discarded_bytes` | 音频设备对象是否存在、恢复层累计故障次数、设备不可用时丢弃的 PCM 字节 |

峰值在每次采样后重置；累计计数不会重置，音视频后台实例重建除外。音频 stream 编号是本次采样的活动流顺序，流创建/撤销或设备恢复后要重新对齐，不能跨设备重建直接相减。不支持某字段的后台会缺省该字段，不能当作 0；本次 WinCE MRN PCM 路径提供音频队列数据，Windows 后台和 buffered AAC 路径不提供相同队列统计。

时钟使用系统毫秒刻度；低于分辨率的工作可能显示 0，线程抢占也计入耗时。批次工作归入完成时的采样窗口。日志只记录数字和状态，没有逐帧写盘；后台记录仍消耗 CPU 和存储带宽，不能认为完全无开销。桥接触摸日志保留事件处理时刻，每秒批量写出，突然断电可能丢失末尾未写出的记录。

## 判断方向

普通 AAC 新增 `codec=AAC-LC`、`aac_packets`、`aac_decoded`（处理的压缩包数）、`aac_pcm_bytes`、`aac_q_packets`、`aac_q_bytes`、`aac_dropped_packets`、`aac_decode_ms_total`、`aac_decode_max_ms`。解码计时在独立线程，原有 `audio_process_ms` 只包含接收侧解密/入队，不再包含 AAC 解码。解码和丢包计数为累计值；`aac_error` 记录后台失败。PCM 路径标记 `codec=PCM16`。

会话报告 `aac_offered` / `buffered_aac_offered` 记录声明能力；`audio_codec` / `audio_stream_type` 是最近一次普通音频协商结果，多路并存应以逐路 `MEDIA_AUDIO codec` 为准。`AAC_PROBE` 失败时只说明此启动未启用 AAC，不能把随后的 PCM 播放当作 AAC 测试成功。输入/输出关闭状态分别见 `microphone_enabled` / `audio_output_enabled`。

- 持续动画时，N2/N3 增长稳定，但视频队列变深、等待变长、supplied 增长慢：优先查解码/显示下游的背压。
- N2/N3 增长慢、队列很浅、submit 很短：上游来流或接收线程调度值得查；不能仅凭这些字段区分手机停发、NCM、TCP 和车机调度。
- parse 耗时明显增加：解析、鉴权、解密、组包所在阶段占用变大；不是单独的 CPU 使用率测量。
- 各阶段累计数量相近，但间隔出现明显尖峰：平均帧率可能正常，帧间节奏仍然会卡。

接收码率 Mbps = `rx_bytes_delta * 8 / (window_ms * 1000)`，窗口为 0 时不计算。它包含视频协议头和加密开销，不包含所有 USB/TCP/IP 开销，也不是 NCM 极限带宽。单次正常播放日志能缩小问题范围，不能保证所有未知环节一次完全定位。

## 开发验证

构建 `carplay/build.ps1 -Target CeMrn`，运行 `python firmware/mrn/package.py` 生成同目录配套 EXE/DLL 更新包；需要已有 `build/tools/file_ops/file_ops.exe` 和通过验证的桥接 DLL。便携统计回归覆盖不等长窗口、累计/增量、空闲、时钟回绕、分片与鉴权失败；Windows 合成播放验证 60 帧完整接收、解密、提交和正常音频。通过电脑测试及 CE 编译不代表实车 USB/解码/显示性能已验收。
