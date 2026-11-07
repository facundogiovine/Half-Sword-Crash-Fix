// Forward every XInput1_4 export to the real Windows XInput1_4.dll.
// Generated from: dumpbin /exports C:\Windows\System32\XInput1_4.dll
#define FWD "/export:"
#define SYS "=\\\\.\\GLOBALROOT\\SystemRoot\\System32\\XInput1_4.dll."
#pragma comment(linker, FWD "XInputEnable" SYS "XInputEnable,@5")
#pragma comment(linker, FWD "XInputGetAudioDeviceIds" SYS "XInputGetAudioDeviceIds,@10")
#pragma comment(linker, FWD "XInputGetBatteryInformation" SYS "XInputGetBatteryInformation,@7")
#pragma comment(linker, FWD "XInputGetCapabilities" SYS "XInputGetCapabilities,@4")
#pragma comment(linker, FWD "XInputGetKeystroke" SYS "XInputGetKeystroke,@8")
#pragma comment(linker, FWD "XInputGetState" SYS "XInputGetState,@2")
#pragma comment(linker, FWD "XInputSetState" SYS "XInputSetState,@3")
#pragma comment(linker, FWD "#100" SYS "#100,@100,NONAME")
#pragma comment(linker, FWD "#101" SYS "#101,@101,NONAME")
#pragma comment(linker, FWD "#102" SYS "#102,@102,NONAME")
#pragma comment(linker, FWD "#103" SYS "#103,@103,NONAME")
#pragma comment(linker, FWD "#104" SYS "#104,@104,NONAME")
#pragma comment(linker, FWD "#108" SYS "#108,@108,NONAME")
#pragma comment(linker, FWD "#109" SYS "#109,@109,NONAME")
