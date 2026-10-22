# Lightrod

Native C++ rod macro integrated into the supplied Fusion Lunar Client 1.8.9 DLL. The in-game interface uses Fusion's existing Win32/OpenGL ImGui hooks, in dark grey and off-white. Build outputs are **`dist/Lightrod-Injector.exe`**, **`dist/Lightrod.dll`**, and their **`dist/Lightrod.zip`** bundle. The original source archive and batch file in Downloads were left unchanged; their working copies are here.

## Operate

Extract `dist/Lightrod.zip` and keep `Lightrod-Injector.exe` and `Lightrod.dll` together. Start Lunar Client 1.8.9 and wait for its game window. Run the injector, click **Refresh**, select Lunar's Java process by its window title/PID, then click **Inject Lightrod**. Return to the game and press **Insert**. The DLL can also be loaded through the project's prior DLL-loading workflow.

The injector checks the target executable, x64 architecture and loaded JVM, verifies the DLL's PE architecture, and rejects duplicate loads. Loading runs on a background worker. It resolves the target Windows loader module with its own ASLR base and keeps the DLL path allocated until loading finishes. A successful load confirms the DLL was present in the target module table; it does not establish that the client's mappings or in-game initialization are compatible. Failures appear in the injector window. Use the same permission level as Lunar if Windows denies process access.

- **Insert:** open/close the in-game menu. **Escape:** cancel binding capture first; otherwise close the menu.
- **Hold Z:** repeat the sword/rod sequence while enabled and in focused gameplay.
- **F8:** independently enable/disable the macro. It starts enabled.
- Click a binding to capture its next key. Trigger and toggle also accept mouse buttons; sword/rod inputs accept keyboard keys. Conflicts are explained and rejected during capture; conflicting loaded settings suspend execution until corrected.
- Click a phase action to cycle **None / Left / Right**.
- **Return to sword after rod** is enabled by default. It selects the sword after the rod action without another click, including when you release the trigger during the rod step in gameplay. Turn it off to return only when the next held cycle starts. It is saved with the other settings.
- Rapid trigger taps cannot restart a cycle before its configured period or queue extra cycles. Sword cleanup finishes once before another cycle may start. Insert and the enable/disable key ignore held-key repeat and presses within a 150 ms bounce interval.
- Edit the timing fields directly or use their minus/plus buttons. Lower **Delay 1** and **Delay 2** repeat faster. Ctrl modifies the step to 10 ms.
- Changes apply and save immediately. **Save** explicitly retries persistence; **Reset** restores and saves all defaults, including enabled state.
- The menu and small gameplay status display show **OFF / READY / RUNNING**. Opening the menu suspends execution. After a screen/focus/configuration change, release and press the trigger again to resume.
- **Unload Lightrod** cancels input, saves, restores cursor capture where appropriate, and detaches the existing hooks.

Settings: `%APPDATA%\Lightrod\settings.cfg`. Runtime failures are written to `%APPDATA%\Lightrod\lightrod.log` and the Windows debugger output. No console window is allocated for Lightrod diagnostics.

## Sequence and defaults

| Setting | Default |
| --- | --- |
| Enabled / trigger / toggle | On / Z / F8 |
| Sword / rod selection inputs | 1 / 2 |
| Sword / rod click actions | None / Right |
| Return to sword after rod | On |
| Key hold / click synchronization | 20 ms / 5 ms |
| Delay 1 / Delay 2 | 69 ms / 69 ms |

The default event trace is:

```text
  0 ms  sword key down
 20 ms  sword key up
 69 ms  rod key down
 74 ms  right mouse down
 94 ms  right mouse up, rod key up
 94 ms  sword key down (selection only)
114 ms  sword key up
138 ms  sword key down for the next cycle
```

Both phase clicks obey synchronization when configured. Each phase releases mouse before its selection key. The return option sends only a selection key; it never executes the sword phase's configured click. Cycle targets use monotonic timestamps: rod at `cycleStart + delay1`, next cycle at `cycleStart + delay1 + delay2`. Actual holds start after input delivery. Missed periods are skipped after a stall instead of replaying a burst. At very short delays, the additional return-key hold can extend the target period.

Releasing the trigger cancels rod/click inputs promptly, then finishes a single sword selection if the return option is on and gameplay is still valid. Disabling the feature, opening a menu, losing focus, changing settings, or encountering an input error uses hard cancellation with no additional selection. Rapid taps during a return cannot overlap or duplicate its key press. The next activation must respect the previous cycle's time budget; taps released during that wait are not queued.

Effective hold follows the batch source exactly:

```text
max(1, min(configuredHold, min(delay1, delay2) - sync - 1))
```

Hold is clamped to 1–2000 ms, sync to 0–2000 ms, and delays to 10–2000 ms. If synchronization exceeds a phase's budget, the menu explains that the requested cycle target cannot be met. Cancellation still interrupts the wait. Real game ticks and Windows scheduling limit input consumption; very short timings do not guarantee that Minecraft will execute every queued action.

## Integration and files

Paths below are relative to `fusion-main/` unless stated otherwise.

| File(s) | Purpose |
| --- | --- |
| `fusion/src/base/moduleManager/modules/lightrod/engine.h` | Non-blocking, testable timing state machine; source order and hold formula |
| `.../lightrod/lightrod.h`, `.../lightrod/lightrod.cpp` | Module lifecycle, runtime status, bindings, focus/screen gates, owned-input cleanup, window input delivery, cursor capture |
| `.../lightrod/config.h`, `.../lightrod/config.cpp` | Validated independent fields; temporary-file replacement; preserves valid entries when other fields are malformed |
| `.../lightrod/pressGate.h` | Physical-press edge detection and independent 150 ms bounce guards for shortcuts |
| `fusion/src/base/menu/lightrodMenu.cpp` | All controls, tooltips, auto-save, reset, status, hover/entrance/exit animations |
| `fusion/src/base/menu/menu.h`, `menu.cpp` | Atomic shared state and safe backend teardown |
| `fusion/src/base/menu/hooks/wndProc.cpp` | Insert/Escape, input capture, input consumption, synthetic event translation, fullscreen cleanup |
| `fusion/src/base/menu/hooks/wglSwapBuffers.cpp` | Existing in-game render hook, font fallback, palette, cursor clipping/context lifecycle |
| `fusion/src/base/menu/renderMenu.cpp` | Keeps original Fusion menu implementation under a separate unused entry point |
| `fusion/src/base/moduleManager/moduleManager.cpp` | Register/update Lightrod once; keep unrelated Fusion modules inactive |
| `fusion/src/base/base.cpp`, `base.h`, `fusion/src/main.cpp` | Single worker, balanced 1 ms timer request, cancellation before shutdown, exception/local-reference handling, correctly typed startup callback |
| `fusion/src/base/java/java.cpp` | Resolve the existing Minecraft class loader using Fusion's loaded-class discovery; fail cleanly on missing classes |
| `fusion/src/base/sdk/net/minecraft/client/Minecraft.cpp` | Check the original singleton mapping before invoking it |
| `fusion/src/base/util/math/geometry.h` | Add the missing math header required by the portable compiler |
| `fusion/src/base/renderLoop.cpp` | Lightrod gameplay status display |
| `fusion/ext/imgui/imgui_widgets.cpp` | Remove Fusion's hard-coded 238 px subtraction from text-input width so numeric controls remain usable |
| `CMakeLists.txt`, `fusion/fusion.vcxproj`, `fusion.sln` | Build integration and Lightrod output name |
| `tests/lightrod_tests.cpp` | Deterministic sequence, timing, cancellation, failure and persistence tests |
| `tests/menu_smoke.cpp` | Hidden OpenGL test of the production menu and controller; no live-game claim |
| `injector/main.cpp`, `injector/loader.h`, `injector/loader.cpp` | Native injector GUI, process selection, compatibility checks and standard Windows DLL loading |
| `tests/loader_tests.cpp`, `tests/loader_probe.cpp` | Load a harmless DLL into a disposable test host; check duplicate, missing/invalid DLL and target failures |
| Root `build.ps1`, `.gitignore`, `README.md` | Reproducible build/test/package workflow and this report |
| Root `Rod Macro.bat` | Unmodified behavioral reference, read in full |

Fusion is a Windows C++20 / Visual Studio v143 project, with JNI/JVMTI wrappers using its original named 1.8.9 mappings. The optional ASM Maven project targets Java 8; Lightrod needs no ASM changes or new Java bytecode. The existing gameplay JNI checks run only on Fusion's attached worker thread with bounded local reference frames.

Input delivery extends Fusion's existing window-message input path. A private registered message dispatches normal keyboard/mouse press/release messages to the original LWJGL window procedure on its window thread, with cancellation generations and bounded dispatch waits. It changes no inventory state, attributes, durability, or packets. Fullscreen window replacement releases delivered inputs before restoring the old procedure. Failed releases are retried before new actions; unload waits for acknowledgement rather than removing a handler while inputs remain pending.

Cursor release/restoration uses the documented LWJGL 2 [`Mouse.isGrabbed` / `Mouse.setGrabbed`](https://legacy.lwjgl.org/javadoc/org/lwjgl/input/Mouse.html) API, resolved from the loaded runtime. The input message format was checked against LWJGL's [WindowsDisplay source](https://github.com/LWJGL/lwjgl/blob/master/src/java/org/lwjgl/opengl/WindowsDisplay.java). Helvetica is used when installed under the expected system font filename, otherwise Arial, then ImGui's default font. Fonts are not bundled.

Fusion's original ESP/combat/clicker code is preserved but inactive and unavailable from the Lightrod menu. The unrelated reach bytecode patcher is not initialized.

## Build and verification

From this folder in PowerShell:

```powershell
.\build.ps1
.\build.ps1 -RenderTest
```

The script uses the workspace's portable GCC toolchain when present, otherwise CMake's installed Visual Studio x64 toolchain. Requirements: CMake, a Windows x64 C++20 compiler, and a Windows SDK/toolchain. `-RenderTest` also needs a local x64 `jvm.dll` from Lunar's JRE or Eclipse Adoptium; it creates only a hidden test window and saves `build/menu-preview.png`. A fresh toolchain selection may require a separate build directory if an existing CMake cache uses a different generator.

The original solution can also be built with Visual Studio v143 in **Release / x64**, producing `fusion-main/fusion/build/Lightrod.dll`. That MSVC path was updated but not executed in this environment. The verified build used portable GCC 16.2.0 through CMake, with static compiler runtimes. Some warnings remain in unchanged, inactive Fusion code.

Verified on 22 October 2026:

- Release x64 DLL compiled and linked, including the existing hook, SDK and input integration.
- **27 behavior test groups passed**, including cancellation at every millisecond across multiple phases/cycles, every click combination, changed selection inputs, faster delays, missed-worker periods, mouse delivery failures, persisted enabled/return settings, malformed fields, and failure-safe saving. Return-on/off traces, trigger release throughout the rod step, rapid tap/re-press stress, return delivery/cleanup failures, absence of extra clicks, and shortcut repeat/bounce guards are covered.
- Production menu rendered on a hidden native OpenGL surface with the actual font/style setup. The saved render was visually inspected; timing inputs, bindings, click controls and footer are visible.
- The render/controller harness also passed entrance/exit, smaller-viewport rendering, mouse binding capture, rejection of mouse slot bindings, Escape cancellation, enable/disable state changes and clearing capture on close.
- DLL inspected as PE32+ x64 with Windows/JVM imports and no separate GCC runtime DLL requirement.
- Injector EXE compiled as x64 Windows GUI with static compiler runtimes. Its process selection window passed a hidden creation/close test. The loader passed an actual DLL load into its own disposable test process, plus duplicate-load, invalid-PE, non-Java-target, invalid-PID and missing-DLL rejection tests. The injector has not been run against Lunar.
- Diff reviewed against the supplied archive. No core feature TODOs or placeholder methods remain.

**Not live-tested:** casting and slot selection in a running Lunar session, physical F8/Insert event delivery, chat/inventory transitions, actual focus loss, fullscreen changes, and unload during a real game. No Minecraft JVM was running. Fusion is unmaintained and only targets its original Lunar 1.8.9 mappings; compatibility with a current Lunar build or another Minecraft version is not established by compilation or the render test. Missing mappings abort initialization; unavailable cursor APIs suspend execution.
