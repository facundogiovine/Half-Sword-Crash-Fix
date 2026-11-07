// Fakes the game's crashes to check the fix. Build and run with build.bat test.
// Uses the real handler from paintfix.c, pointed at fake call paths in this exe:
// one for the paint thread spot, one for the game thread spot.

#define PAINTFIX_TEST
#include "paintfix.c"
#include <intrin.h>
#include <stdio.h>

static int g_failures;

static void Check(BOOL ok, const char *what)
{
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        g_failures++;
}

typedef struct {
    volatile float *data;
    int spot;  // which safe spot's chain to record into, or -1 to record nothing
    BOOL result;
    BOOL crashed;
    BOOL registersKept;
} Job;

// Records a return address into a spot's call chain the first time it is seen.
static void Record(const Job *job, int index, void *returnAddress)
{
    if (job->spot >= 0 && !g_spots[job->spot].chain[index])
        g_spots[job->spot].chain[index] = (DWORD64)returnAddress;
}

// Stands in for the plugin reading a vertex position from a mesh buffer.
__declspec(noinline) static float ReadVertex(volatile float *data, int index)
{
    return data[index * 3] + data[index * 3 + 1] + data[index * 3 + 2];
}

// Stands in for the plugin code that reads the mesh (vertex check or result callback).
__declspec(noinline) static BOOL FakeMeshRead(Job *job, float *out)
{
    Record(job, 0, _ReturnAddress());
    float sum = 0;
    for (int i = 0; i < 4; i++)
        sum += ReadVertex(job->data, i);
    *out = sum;
    return TRUE;
}

// Calls the mesh read and checks that the caller's saved registers survive.
#define RUN_CHECK_BODY                                                   \
    Record(job, 1, _ReturnAddress());                                    \
    volatile int seed = 7;                                               \
    int a = seed * 3, b = seed * 5, c = seed * 11;                       \
    float out = 0;                                                       \
    __try {                                                              \
        job->result = FakeMeshRead(job, &out);                           \
    } __except (EXCEPTION_EXECUTE_HANDLER) {                             \
        job->crashed = TRUE;                                             \
    }                                                                    \
    job->registersKept = (a == 21 && b == 35 && c == 77);

static volatile int g_sink;

// Paint thread path: PaintThread -> PaintTask -> PaintCheck -> FakeMeshRead.
__declspec(noinline) static void PaintCheck(Job *job) { RUN_CHECK_BODY }
__declspec(noinline) static void PaintTask(Job *job)
{
    Record(job, 2, _ReturnAddress());
    PaintCheck(job);
    g_sink++;  // stops the compiler from turning the call into a jump
}

// Game thread path: CallbackThread -> CallbackTask -> CallbackCheck -> FakeMeshRead.
__declspec(noinline) static void CallbackCheck(Job *job) { RUN_CHECK_BODY }
__declspec(noinline) static void CallbackTask(Job *job)
{
    Record(job, 2, _ReturnAddress());
    CallbackCheck(job);
    g_sink++;
}

// Other code reaching the paint check; its crashes must not be hidden.
__declspec(noinline) static void OtherTask(Job *job)
{
    PaintCheck(job);
    g_sink++;
}

static DWORD WINAPI PaintThread(LPVOID param)    { PaintTask((Job *)param); g_sink++; return 0; }
static DWORD WINAPI CallbackThread(LPVOID param) { CallbackTask((Job *)param); g_sink++; return 0; }
static DWORD WINAPI OtherThread(LPVOID param)    { OtherTask((Job *)param); g_sink++; return 0; }

static Job Run(LPTHREAD_START_ROUTINE entry, int spot, volatile float *data)
{
    Job job = { data, spot, -1, FALSE, FALSE };
    HANDLE thread = CreateThread(NULL, 0, entry, &job, 0, NULL);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return job;
}

int main(void)
{
    // Point the handler at this exe instead of the game.
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *headers = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    g_moduleStart = (DWORD64)base;
    g_moduleEnd = g_moduleStart + headers->OptionalHeader.SizeOfImage;
    AddVectoredExceptionHandler(1, OnException);

    // A good mesh buffer, and one that has been freed.
    float *good = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    for (int i = 0; i < 12; i++)
        good[i] = 1.0f;
    float *freed = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    VirtualFree(freed, 0, MEM_RELEASE);

    // The first good runs fill in both call chains.
    Job job = Run(PaintThread, 0, good);
    Check(job.result == TRUE && !job.crashed, "good buffer in paint task works normally");
    job = Run(CallbackThread, 1, good);
    Check(job.result == TRUE && !job.crashed, "good buffer in game thread callback works normally");
    Check(g_spots[0].chain[2] && g_spots[1].chain[2], "both call chains recorded");
    Check(g_catchCount == 0, "nothing caught for good buffers");

    // Paint thread spot.
    job = Run(PaintThread, -1, freed);
    Check(!job.crashed, "freed buffer in paint task does not crash");
    Check(job.result == FALSE, "paint check returns false after the bad read");
    Check(job.registersKept, "paint caller's saved registers survive the recovery");
    Check(g_catchCount == 1, "one catch counted");

    job = Run(PaintThread, -1, good);
    Check(job.result == TRUE && !job.crashed, "paint task keeps working after a catch");

    // Game thread spot.
    job = Run(CallbackThread, -1, freed);
    Check(!job.crashed, "freed buffer in game thread callback does not crash");
    Check(job.registersKept, "callback caller's saved registers survive the recovery");
    Check(g_catchCount == 2, "second catch counted");

    job = Run(CallbackThread, -1, good);
    Check(job.result == TRUE && !job.crashed, "game thread callback keeps working after a catch");

    // Anything else must not be hidden.
    job = Run(OtherThread, -1, freed);
    Check(job.crashed, "same crash reached from other code is NOT hidden");
    Check(g_catchCount == 2, "no catch counted for the other code");

    BOOL crashedOutside = FALSE;
    __try {
        volatile float x = freed[0];
        (void)x;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        crashedOutside = TRUE;
    }
    Check(crashedOutside, "bad read outside the safe spots is NOT hidden");

    printf(g_failures ? "\n%d FAILED\n" : "\nALL PASSED\n", g_failures);
    return g_failures ? 1 : 0;
}
