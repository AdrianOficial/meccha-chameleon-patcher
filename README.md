Working Version: 3.3.2

# chameleon_hook.dll

A small DLL that loads into **Meccha Chameleon** and does three things:

1. **Steam (Spacewar)** — writes `steam_appid.txt = 480` next to the exe at
   startup, so Steam initialises even without owning the game. This makes the
   Steam persona name available (used by the name feature).
2. **Auto-login** — writes `AuthenticationGraph=Anonymous` into the game's
   `Engine.ini` at startup, so you get past the "Retry" login error with no Epic
   account, **from the first launch, on any machine, with no manual step**. This
   part does not depend on any game addresses, so it keeps working across game
   updates.
3. **Name** — hooks the game's EOS nickname getters and returns your **Steam
   name** instead of the default. (Note: the big replicated name above your
   avatar in multiplayer is set in-game, not here — this changes the EOS
   nickname, which may not be the visible one. Auto-login is the part that
   reliably matters.)

Steps 1 and 2 are address-independent and always work; only step 3 depends on
game-build offsets.

The name hooks use hard-coded addresses (RVAs) that are specific to one game
build.

---

## How to use it

There are two ways to get the DLL into the game.

### A) Permanent — auto-load with the game (recommended)

Make the game load the DLL by itself every launch, no injector:

1. Copy the DLL next to the game exe:
   ```
   copy chameleon_hook.dll  ->  <Game>\Chameleon\Binaries\Win64\
   ```
2. From the repo root, add the DLL as a static import to the exe:
   ```
   python python/add_import.py
   ```
3. Launch the game normally. Done.

To undo it and get the clean exe back:
```
python python/add_import.py --restore
```

### B) Temporary — inject for testing

Load the DLL for one run without modifying the exe (uses `inject.exe`):

```
inject.exe launch "<Game>\Chameleon\Binaries\Win64\PenguinHotel-Win64-Shipping.exe" chameleon_hook.dll
```
Or attach to the game while it's already running:
```
inject.exe attach PenguinHotel-Win64-Shipping.exe chameleon_hook.dll
```

---

## How to compile

The DLL is a single C file (`chameleon_hook.c`) with no external dependencies —
it only uses the Windows API. Build it with **zig** (`zig cc`, a self-contained
C/C++ compiler; download it from https://ziglang.org/download/, no install
needed):

```
zig cc -target x86_64-windows-gnu -shared -O2 -o chameleon_hook.dll chameleon_hook.c
```

The DLL must export `ChameleonInit` (it already does) so `add_import.py` can
import it. That's the only requirement.

> Any other compiler works too, as long as it produces a 64-bit Windows DLL that
> exports `ChameleonInit`. For example, with MSVC:
> ```
> cl /LD /O2 chameleon_hook.c /link /EXPORT:ChameleonInit
> ```

---

## Updating after a game update (name hooks only)

A game update shifts every address, so the name hooks go dormant (auto-login
still works). To turn them back on, put the new RVAs into `chameleon_hook.c` and
recompile:

- `g_rva[0]` — `execGetPlayerNickname`
- `g_rva[1]` — the C++ nickname getter it calls
- `WFSTRING_CTOR_RVA` — the `FString(const wchar_t*)` constructor

---

## What's inside (quick tour of `chameleon_hook.c`)

- `ensure_steam_appid()` — writes `steam_appid.txt = 480` next to the exe so
  Steam starts (Spacewar). Runs in `DllMain`, before `SteamAPI_Init`.
- `ensure_auth_ini()` — the auto-login; runs in `DllMain` (before the engine
  reads its config) so the setting is always present.
- `steam_persona()` / `put_steam_name()` — read the Steam name and build a game
  `FString` from it using the game's own wide-string constructor.
- `hook_getnick` / `hook_nickimpl` — call the original getter, then overwrite the
  returned name with the Steam name.
- `install()` — inline-hooks a target: verifies the expected bytes (`g_expect`).