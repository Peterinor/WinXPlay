#pragma once
namespace wxui {
struct Label { int size; const wchar_t* text; const wchar_t* english; };
static const Label Brand={34,L"WinXPlay",L"WinXPlay"};
static const Label Waiting={30,L"\u7b49\u5f85\u8fde\u63a5 iPhone",L"Waiting for iPhone"};
static const Label WaitingHint={20,L"\u8fde\u63a5\u6570\u636e\u7ebf\u5e76\u89e3\u9501\u624b\u673a\uff0c\u5373\u53ef\u81ea\u52a8\u6295\u5c4f",L"Connect the cable and unlock your iPhone"};
static const Label Preparing={30,L"\u6b63\u5728\u51c6\u5907\u8fde\u63a5",L"Preparing connection"};
static const Label PreparingHint={20,L"\u6b63\u5728\u51c6\u5907 USB \u901a\u9053\uff0c\u8bf7\u4fdd\u6301\u6570\u636e\u7ebf\u8fde\u63a5",L"Preparing USB. Keep the cable connected"};
static const Label Trust={30,L"\u8bf7\u5728\u624b\u673a\u4e0a\u786e\u8ba4",L"Confirm on iPhone"};
static const Label TrustHint={20,L"\u89e3\u9501 iPhone\uff0c\u51fa\u73b0\u63d0\u793a\u65f6\u9009\u62e9\u201c\u4fe1\u4efb\u201d\u6216\u201c\u5141\u8bb8\u201d",L"Unlock iPhone, then tap Trust or Allow"};
static const Label Network={30,L"\u6b63\u5728\u5efa\u7acb\u8fde\u63a5",L"Establishing connection"};
static const Label NetworkHint={20,L"\u7b49\u5f85\u624b\u673a\u7f51\u7edc\u5c31\u7eea\uff0c\u968f\u540e\u81ea\u52a8\u5f00\u59cb\u6295\u5c4f",L"Waiting for the phone network"};
static const Label Video={30,L"\u5373\u5c06\u5f00\u59cb\u6295\u5c4f",L"Starting CarPlay"};
static const Label VideoHint={20,L"\u8fde\u63a5\u5df2\u5efa\u7acb\uff0c\u6b63\u5728\u7b49\u5f85\u624b\u673a\u753b\u9762",L"Connected. Waiting for the phone display"};
static const Label Retry={30,L"\u6b63\u5728\u91cd\u65b0\u8fde\u63a5",L"Reconnecting"};
static const Label RetryHint={20,L"\u8fde\u63a5\u6682\u65f6\u4e2d\u65ad\uff0c\u7cfb\u7edf\u4f1a\u81ea\u52a8\u91cd\u8bd5",L"Connection interrupted. Retrying automatically"};
static const Label Error={30,L"\u8fde\u63a5\u9047\u5230\u95ee\u9898",L"Connection issue"};
static const Label ErrorHint={20,L"\u8bf7\u68c0\u67e5\u624b\u673a\u4e0e\u6570\u636e\u7ebf\uff0c\u7cfb\u7edf\u4f1a\u81ea\u52a8\u91cd\u8bd5",L"Check your phone and cable. Retrying automatically"};
static const Label Locked={30,L"\u8bf7\u89e3\u9501 iPhone",L"Unlock iPhone"};
static const Label LockedHint={20,L"\u4fdd\u6301\u624b\u673a\u89e3\u9501\uff0c\u5b8c\u6210\u6388\u6743\u540e\u5c06\u7ee7\u7eed\u8fde\u63a5",L"Keep iPhone unlocked to finish authorization"};
static const Label StepUsb={18,L"\u8fde\u63a5\u624b\u673a",L"Connect phone"};
static const Label StepTrust={18,L"\u624b\u673a\u6388\u6743",L"Trust phone"};
static const Label StepLink={18,L"\u5efa\u7acb\u8fde\u63a5",L"Connect"};
static const Label StepVideo={18,L"\u5f00\u59cb\u6295\u5c4f",L"CarPlay"};
static const Label Notice={15,L"\u8fde\u63a5\u63d0\u793a",L"Connection details"};
static const Label Stopped={30,L"\u6b63\u5728\u7ed3\u675f\u8fde\u63a5",L"Disconnecting"};
static const Label StoppedHint={20,L"\u8bf7\u7a0d\u5019",L"Please wait"};
static const Label Paused={30,L"\u81ea\u52a8\u8fde\u63a5\u5df2\u6682\u505c",L"Auto-connect paused"};
static const Label PausedHint={20,L"\u901a\u8fc7\u8bbe\u7f6e\u6062\u590d\u81ea\u52a8\u8fde\u63a5\uff0c\u5373\u53ef\u7ee7\u7eed\u6295\u5c4f",L"Resume auto-connect from settings to continue"};
static const Label Detecting={30,L"\u6b63\u5728\u68c0\u6d4b\u8bbe\u5907",L"Checking device"};
static const Label DetectingHint={20,L"\u8bf7\u4fdd\u6301\u624b\u673a\u8fde\u63a5\uff0c\u68c0\u6d4b\u7ed3\u679c\u5c06\u5728\u8fd9\u91cc\u663e\u793a",L"Keep iPhone connected. Results will appear here"};
static const Label DiagnosticReady={30,L"\u8bbe\u5907\u68c0\u6d4b\u7ed3\u679c",L"Device check results"};
static const Label DiagnosticHint={20,L"\u68c0\u6d4b\u5df2\u5b8c\u6210\uff0c\u53ef\u901a\u8fc7\u8bbe\u7f6e\u6062\u590d\u81ea\u52a8\u8fde\u63a5",L"Check finished. Resume auto-connect from settings"};
}
