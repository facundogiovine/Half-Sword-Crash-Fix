# Half-Sword Crash Fix

Fixes the Half Sword crash that happens when you hit an enemy hard

## Install

1. Download `Half-Sword-Crash-Fix.zip` from [Releases](../../releases).
2. Open your Half Sword folder (Steam > right-click Half Sword > Manage > Browse local files).
3. Drop the contents of the zip in there.
4. Play.

To uninstall, delete `HalfswordUE5\Binaries\Win64\XINPUT1_4.dll`.

## The fix

`XINPUT1_4.dll` is loaded by the game automatically and forwards every XInput call to
Windows. It also adds an exception handler that catches **only** this crash, checked by
the exact call stack, and backs out of it:

- **Paint thread:** the per-vertex check returns `false`, the plugin's own "abort this
  paint pass" path.
- **Game thread:** the result callback is skipped, as if the pass had no result.

It logs to `%LOCALAPPDATA%\HalfSwordUE5\Saved\Logs\PaintFix.log`.
