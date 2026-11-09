# Half-Sword Crash Fix

Fixes the Half Sword crash that happens when you hit an enemy hard
(`EXCEPTION_ACCESS_VIOLATION` in the *Runtime Vertex Paint and Detection Plugin*).
Blood and wound painting stay exactly as they are.

Fan-made, not from the Half Sword developers. Only for game build **CL-2705**; on
any other build it switches itself off.

## Install

1. Download `Half-Sword-Crash-Fix.zip` from [Releases](../../releases).
2. Open your Half Sword folder (Steam > right-click Half Sword > Manage > Browse local files).
3. Drop the contents of the zip in there.
4. Play.

To uninstall, delete `HalfswordUE5\Binaries\Win64\XINPUT1_4.dll`.

## The bug

The plugin paints blood on background threads, reading the mesh's CPU vertex data
(positions, skin weights, tangents, UVs). Sometimes the game frees that data while a
paint task is still using it, and the next read crashes the game. 

## The fix

`XINPUT1_4.dll` is loaded by the game automatically and forwards every XInput call to
Windows. It also adds an exception handler that catches **only** this crash, checked by
the exact call stack, and backs out of it:

- **Paint thread:** the per-vertex check returns `false`, the plugin's own "abort this
  paint pass" path.
- **Game thread:** the result callback is skipped, as if the pass had no result.

It logs to `%LOCALAPPDATA%\HalfSwordUE5\Saved\Logs\PaintFix.log`.

## For the developers

A real fix would keep the mesh data alive for as long as a paint task uses it. For
example, copy the needed vertex data before starting the task, hold a reference to the
render data during the task, or cancel running tasks when the mesh changes and check
in the result callback that it is still the same mesh.

## Build

Needs Visual Studio with *Desktop development with C++*. Run `build.bat` to build
`out\XINPUT1_4.dll`, or `build.bat test` to also run the tests.
