#pragma once
namespace wxui {
struct Label { int size; const wchar_t* text; const wchar_t* english; };
static const Label Brand={34,L"WinXPlay",L"WinXPlay"};
static const Label Waiting={30,L"等待连接 iPhone",L"Waiting for iPhone"};
static const Label WaitingHint={20,L"连接数据线并解锁手机，即可自动投屏",L"Connect the cable and unlock your iPhone"};
static const Label Preparing={30,L"正在准备连接",L"Preparing connection"};
static const Label PreparingHint={20,L"正在准备 USB 通道，请保持数据线连接",L"Preparing USB. Keep the cable connected"};
static const Label Trust={30,L"请在手机上确认",L"Confirm on iPhone"};
static const Label TrustHint={20,L"解锁 iPhone，出现提示时选择“信任”或“允许”",L"Unlock iPhone, then tap Trust or Allow"};
static const Label Network={30,L"正在建立连接",L"Establishing connection"};
static const Label NetworkHint={20,L"等待手机网络就绪，随后自动开始投屏",L"Waiting for the phone network"};
static const Label Video={30,L"即将开始投屏",L"Starting CarPlay"};
static const Label VideoHint={20,L"连接已建立，正在等待手机画面",L"Connected. Waiting for the phone display"};
static const Label Retry={30,L"正在重新连接",L"Reconnecting"};
static const Label RetryHint={20,L"连接暂时中断，系统会自动重试",L"Connection interrupted. Retrying automatically"};
static const Label Error={30,L"连接遇到问题",L"Connection issue"};
static const Label ErrorHint={20,L"请检查手机与数据线，系统会自动重试",L"Check your phone and cable. Retrying automatically"};
static const Label Locked={30,L"请解锁 iPhone",L"Unlock iPhone"};
static const Label LockedHint={20,L"保持手机解锁，完成授权后将继续连接",L"Keep iPhone unlocked to finish authorization"};
static const Label StepUsb={18,L"连接手机",L"Connect phone"};
static const Label StepTrust={18,L"手机授权",L"Trust phone"};
static const Label StepLink={18,L"建立连接",L"Connect"};
static const Label StepVideo={18,L"开始投屏",L"CarPlay"};
static const Label Notice={15,L"连接提示",L"Connection details"};
static const Label Stopped={30,L"正在结束连接",L"Disconnecting"};
static const Label StoppedHint={20,L"请稍候",L"Please wait"};
static const Label Paused={30,L"自动连接已暂停",L"Auto-connect paused"};
static const Label PausedHint={20,L"通过设置恢复自动连接，即可继续投屏",L"Resume auto-connect from settings to continue"};
static const Label Detecting={30,L"正在检测设备",L"Checking device"};
static const Label DetectingHint={20,L"请保持手机连接，检测结果将在这里显示",L"Keep iPhone connected. Results will appear here"};
static const Label DiagnosticReady={30,L"设备检测结果",L"Device check results"};
static const Label DiagnosticHint={20,L"检测已完成，可通过设置恢复自动连接",L"Check finished. Resume auto-connect from settings"};
}
