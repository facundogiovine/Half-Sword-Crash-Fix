// Half Sword paint crash fix.
// Loaded by the game as XINPUT1_4.dll. All real XInput calls are forwarded to Windows.
// (Not dwmapi.dll, so it does not clash with UE4SS.)
//
// The blood painting plugin sometimes reads mesh data the game has already freed.
// When that happens in one of the two known spots, we leave that call early instead
// of crashing:
//  - paint threads: the per-vertex check returns false (the plugin's own
//    "give up on this paint pass" path);
//  - game thread: the callback that applies a finished paint pass is skipped,
//    as if the pass had no result.

#include <windows.h>
#include <string.h>
#include <wchar.h>

#ifndef PAINTFIX_TEST
#include "exports.h"
#endif

// Offsets inside HalfSwordUE5-Win64-Shipping.exe (build CL-2705).
#define VERTEX_CHECK_RVA   0x4A33A00  // per-vertex paint check, returns bool
#define CALL_SITE_RVA      0x4A28300  // the one call to it from the paint task
#define MAX_UNWIND_FRAMES  32
#define CHAIN_LENGTH       3

// A place where a crash is caught. `chain` holds return addresses, innermost first,
// as seen in the crash reports; the crash is only caught when the stack matches all
// of them. We continue right after the call to chain[0], with RAX set to `result`.
typedef struct {
    const char *name;
    DWORD chainRvas[CHAIN_LENGTH];
    DWORD64 result;
    DWORD64 chain[CHAIN_LENGTH];  // filled in at install
} SafeSpot;

static SafeSpot g_spots[] = {
    { "paint thread", { 0x4A28305, 0x4A2DF86, 0x4A57F34 }, 0 },  // vertex check returns false
    { "game thread",  { 0x2118DF0, 0x4A51FC1, 0x2FDD8C6 }, 0 },  // paint result callback, returns nothing
};
#define SPOT_COUNT  ((int)(sizeof(g_spots) / sizeof(g_spots[0])))

// First bytes of the vertex check, used to make sure this is the right game build.
static const BYTE kVertexCheckStart[] = {
    0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
};

static DWORD64 g_moduleStart;
static DWORD64 g_moduleEnd;
static volatile LONG g_catchCount;
static WCHAR g_logPath[MAX_PATH];

static void WriteLog(const char *text)
{
    if (!g_logPath[0])
        return;
    HANDLE file = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    char line[512];
    int length = wsprintfA(line, "[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n",
                           now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, text);
    DWORD written;
    WriteFile(file, line, length, &written, NULL);
    CloseHandle(file);
}

// Moves `context` up one stack frame. Returns FALSE if that is not possible.
static BOOL UnwindOneFrame(CONTEXT *context, BOOL firstFrame)
{
    if (context->Rip < g_moduleStart || context->Rip >= g_moduleEnd)
        return FALSE;

    DWORD64 imageBase;
    PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context->Rip, &imageBase, NULL);
    if (function) {
        PVOID handlerData;
        DWORD64 establisherFrame;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context->Rip, function,
                         context, &handlerData, &establisherFrame, NULL);
        return TRUE;
    }
    if (firstFrame) {
        // Leaf function: the return address is right on top of the stack.
        context->Rip = *(DWORD64 *)context->Rsp;
        context->Rsp += 8;
        return TRUE;
    }
    return FALSE;
}

// True if the frames above `context` match the rest of the spot's chain.
static BOOL OuterChainMatches(const CONTEXT *context, const SafeSpot *spot)
{
    CONTEXT outer = *context;
    for (int i = 1; i < CHAIN_LENGTH; i++) {
        if (!UnwindOneFrame(&outer, FALSE) || outer.Rip != spot->chain[i])
            return FALSE;
    }
    return TRUE;
}

// Walks up the stack from the crash. If the crash is inside a known safe spot,
// fills `context` with the state right after that call returned and returns the spot.
static const SafeSpot *UnwindToSafeSpot(CONTEXT *context)
{
    for (int frame = 0; frame < MAX_UNWIND_FRAMES; frame++) {
        if (!UnwindOneFrame(context, frame == 0))
            return NULL;
        for (int s = 0; s < SPOT_COUNT; s++) {
            if (context->Rip == g_spots[s].chain[0])
                return OuterChainMatches(context, &g_spots[s]) ? &g_spots[s] : NULL;
        }
    }
    return NULL;
}

static LONG CALLBACK OnException(EXCEPTION_POINTERS *info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;

    CONTEXT context = *info->ContextRecord;
    const SafeSpot *spot = UnwindToSafeSpot(&context);
    if (!spot)
        return EXCEPTION_CONTINUE_SEARCH;

    // Leave the crashing call and carry on from its caller.
    context.Rax = spot->result;
    *info->ContextRecord = context;

    LONG count = InterlockedIncrement(&g_catchCount);
    if (count <= 200) {
        char text[200];
        wsprintfA(text, "Caught paint crash #%ld on the %s (bad read at 0x%I64X). Skipped one paint pass.",
                  count, spot->name, (DWORD64)info->ExceptionRecord->ExceptionInformation[1]);
        WriteLog(text);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void SetLogPath(void)
{
    WCHAR localAppData[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH - 64)
        return;
    lstrcpyW(g_logPath, localAppData);
    lstrcatW(g_logPath, L"\\HalfSwordUE5\\Saved\\Logs\\PaintFix.log");
}

// True if the bytes right before `rva` are a call instruction, so `rva` is a real
// return address in this build.
static BOOL FollowsCall(const BYTE *base, DWORD rva)
{
    const BYTE *p = base + rva;
    if (p[-5] == 0xE8)
        return TRUE;  // call rel32
    if (p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0)
        return TRUE;  // call reg
    if (p[-3] == 0xFF && (p[-2] & 0xF8) == 0x50)
        return TRUE;  // call [reg+disp8]
    return FALSE;
}

// Checks the game build and turns the fix on. Returns a message for the log.
static const char *Install(HMODULE game)
{
    BYTE *base = (BYTE *)game;
    IMAGE_NT_HEADERS *headers = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    DWORD imageSize = headers->OptionalHeader.SizeOfImage;
    if (imageSize <= VERTEX_CHECK_RVA + sizeof(kVertexCheckStart))
        return "Disabled: unknown game build (exe too small).";

    // The call site must be "call VERTEX_CHECK_RVA".
    BYTE *call = base + CALL_SITE_RVA;
    INT32 offset = *(INT32 *)(call + 1);
    if (call[0] != 0xE8 || CALL_SITE_RVA + 5 + offset != VERTEX_CHECK_RVA)
        return "Disabled: unknown game build (call site does not match).";
    if (memcmp(base + VERTEX_CHECK_RVA, kVertexCheckStart, sizeof(kVertexCheckStart)) != 0)
        return "Disabled: unknown game build (paint function does not match).";

    for (int s = 0; s < SPOT_COUNT; s++)
        for (int i = 0; i < CHAIN_LENGTH; i++)
            if (!FollowsCall(base, g_spots[s].chainRvas[i]))
                return "Disabled: unknown game build (call chain does not match).";

    g_moduleStart = (DWORD64)base;
    g_moduleEnd = g_moduleStart + imageSize;
    for (int s = 0; s < SPOT_COUNT; s++)
        for (int i = 0; i < CHAIN_LENGTH; i++)
            g_spots[s].chain[i] = g_moduleStart + g_spots[s].chainRvas[i];
    AddVectoredExceptionHandler(1, OnException);
    return "Active: paint crash fix installed for build CL-2705 (paint threads + game thread).";
}

#ifndef PAINTFIX_TEST
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    DisableThreadLibraryCalls(instance);

    // Only act inside the game itself, not in other programs that load XInput.
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (!wcsstr(exePath, L"HalfSwordUE5-Win64-Shipping.exe"))
        return TRUE;

    SetLogPath();
    WriteLog(Install(GetModuleHandleW(NULL)));
    return TRUE;
}
#endif
