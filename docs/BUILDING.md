# Сборка из исходников / Building from source

[Русский](#русский) · [English](#english)

---

## Русский

Готовые сборки лежат в [Releases](https://github.com/bogdanspbm/amnesia-the-dark-descent-coop/releases).
Этот документ — для тех, кто собирает сам. Проверенный путь — **Windows, Win32 (x86), Visual Studio 2022**.
Linux собирается в CI, но в игре не проверялся; macOS — экспериментально.

### Windows

**Что поставить**

1. **Visual Studio 2022** (Community или Build Tools) с нагрузкой **«Разработка классических приложений на C++»**.
   В ней должны быть отмечены:
   - **MSVC v143 – VS 2022 C++ x64/x86 build tools** (в нём и x86/Win32-компилятор);
   - **Windows 10 SDK или Windows 11 SDK** (любой 10.0.x; проверено на 10.0.26100).
   MFC и старые тулсеты (v100) **не нужны** — проект переведён на v143.
2. **Git**.
3. PowerShell 5.1 (встроен в Windows) или PowerShell 7.

Perl для Windows не нужен (Build ID собирается из `cl`); он нужен только для Linux/macOS.

**Сборка**

```powershell
git clone https://github.com/bogdanspbm/amnesia-the-dark-descent-coop.git
cd amnesia-the-dark-descent-coop
.\build\build_windows.ps1
```

Скрипт:
1. находит MSBuild через `vswhere`;
2. распаковывает `HPL2\dependencies.zip` в `HPL2\dependencies\` (готовые библиотеки движка: SDL2, OpenAL, Newton,
   AngelScript, DevIL, GLEW, Ogg/Vorbis/Theora, FLTK);
3. собирает движок `HPL2\core\_HPL2_2010.vcxproj` → `HPL2\lib\HPL2_2010.lib`;
4. собирает игру `amnesia\src\game\Lux.vcxproj` → **`amnesia\redist\Amnesia.exe`**.

Всё — `Release|Win32` с `/p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0`. Без параметров exe
собирается **без Steam** (работает только прямое подключение по IP; кнопки Steam пишут «This build of the game has
no Steam support»).

Вручную то же самое (из «Developer PowerShell for VS 2022»):

```powershell
Expand-Archive HPL2\dependencies.zip HPL2\dependencies
msbuild HPL2\core\_HPL2_2010.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0 /m
msbuild amnesia\src\game\Lux.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0 /m
```

### Steam-лобби (по шагам)

1. Скачайте **Steamworks SDK** на https://partner.steamgames.com/downloads/list (вход любым Steam-аккаунтом).
   Проверена версия **1.65** (`steamworks_sdk_165.zip`).
2. Распакуйте, например, в `C:\SteamworksSDK\`. Нужна папка **`sdk`** — в ней `public\steam\steam_api.h` и
   `redistributable_bin\steam_api.dll` / `steam_api.lib`.
3. Соберите с ней:
   ```powershell
   .\build\build_windows.ps1 -SteamworksSdk C:\SteamworksSDK\sdk
   ```
   или вручную добавьте к сборке `Lux.vcxproj` параметр `/p:SteamworksSdkDir=C:\SteamworksSDK\sdk\`
   (с обратным слешем в конце). Так включается `COOP_STEAM`, линкуется `steam_api.lib`, а `steam_api.dll`
   подгружается отложенно (delay-load).
4. Положите **`sdk\redistributable_bin\steam_api.dll`** в **`<папка игры>\_coop\steam\`**. Рядом с `Amnesia.exe`
   у игры лежит свой, более старый `steam_api.dll` — его **не трогайте**: мод сначала загружает DLL из
   `_coop\steam` по полному пути (путь меняется в `coop.cfg`, `SteamDir="..."`).
5. **AppID.** Своего AppID у мода нет, используется тестовое приложение Valve **Spacewar (480)**. Мод сам
   выставляет переменные окружения `SteamAppId`/`SteamGameId` перед `SteamAPI_InitEx`, поэтому
   **`steam_appid.txt` не нужен**. Другой AppID — `SteamAppId="..."` в `coop.cfg`.
6. При игре должен быть запущен Steam; в профиле будет видно «Spacewar».

`steam_api.dll` и сам SDK в git **не коммитятся** (см. `.gitignore`); DLL кладётся только в релизный zip.

### Упаковка и установка

```powershell
.\build\package_windows.ps1 -Version 0.1.0                                                   # без DLL Steam
.\build\package_windows.ps1 -Version 0.1.0 -SteamDll C:\SteamworksSDK\sdk\redistributable_bin\steam_api.dll
```

Получится `dist\amnesia-coop-0.1.0-windows.zip`:

```
Amnesia_coop.exe            ← amnesia\redist\Amnesia.exe, переименованный
coop.cfg.example
_coop\partner\...           ← coop_data\partner\*
_coop\steam\steam_api.dll   ← только с -SteamDll
INSTALL.txt, LICENSE.txt, CREDITS.md
```

Установка — распаковать в папку игры. Exe называется **`Amnesia_coop.exe`**, чтобы **не перезаписать**
оригинальный `Amnesia.exe`; файлы игры не меняются. Для запуска нужен
[VC++ 2015–2022 Redistributable x86](https://aka.ms/vs/17/release/vc_redist.x86.exe).
Все остальные DLL (SDL2, OpenAL, Newton, DevIL, …) уже есть в папке игры.

### Linux (экспериментально, в игре не проверено)

```bash
sudo apt install build-essential cmake perl libgl1-mesa-dev libglu1-mesa-dev libx11-dev zlib1g-dev
unzip HPL2/dependencies.zip -d HPL2/dependencies
cmake -S amnesia/src -B build-linux -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_POSITION_INDEPENDENT_CODE=OFF -DCMAKE_EXE_LINKER_FLAGS=-no-pie \
      -DCMAKE_CXX_FLAGS="-fpermissive -Wno-narrowing -fno-pie" -DCMAKE_C_FLAGS="-fno-pie"
cmake --build build-linux --target Amnesia -j"$(nproc)"
```

- 64-битная сборка берёт готовые библиотеки из `HPL2/dependencies/lib/linux/lib64`; результат —
  `build-linux/game/Amnesia.bin.x86_64` с rpath `$ORIGIN/lib64`. Кладите его в Linux-версию игры (там уже есть
  `lib64/`) как `Amnesia_coop.bin.x86_64`, а модель — в `_coop/partner/`.
- **`-no-pie` обязателен**: статические библиотеки из `dependencies.zip` собраны без PIC.
- `perl` нужен для `HPL2/core/buildcounter.pl` (генерирует Build ID).
- Steam: `-DSTEAMWORKS_SDK_DIR=<sdk>` (линкуется `redistributable_bin/linux64/libsteam_api.so`; не проверялось).

### macOS (экспериментально)

Готовые библиотеки для macOS — только x86_64 (`lib/macosx64`), проект 2013 года:

```bash
unzip HPL2/dependencies.zip -d HPL2/dependencies
cmake -S amnesia/src -B build-macos -DCMAKE_BUILD_TYPE=Release -DIS_APPLE_64=ON \
      -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
cmake --build build-macos --target Amnesia
```

CI пробует это на Intel-раннере с `continue-on-error`; на Apple Silicon — только через Rosetta.

### FBX

Загрузчик FBX отключён (`HPL2_WITH_FBX=OFF` в CMake, убран из проектов VS): FBX SDK 2012, нужный движку,
больше не распространяется, а игре он не нужен (модели — в `.dae`). Включить: `-DHPL2_WITH_FBX=ON` и свой FBX SDK.

### Частые ошибки

| Ошибка | Причина / решение |
|---|---|
| `MSB8020: The build tools for v100 cannot be found` | Не передан `/p:PlatformToolset=v143` — используйте скрипт или добавьте параметр. |
| `MSB8036: The Windows SDK version 10.0.15063.0 was not found` | Добавьте `/p:WindowsTargetPlatformVersion=10.0` (берётся установленный SDK). |
| `Cannot open include file: 'afxres.h'` | Старый `Lux.rc`; в этом репозитории уже `winres.h`. Проверьте, что собираете эту ветку. |
| `unresolved external symbol __imp___iob_func` / `_sprintf` и т. п. | Старые VS2010-библиотеки (fltk, angelscript): нужен `legacy_stdio_definitions.lib` — уже в `Lux.vcxproj`, плюс `LegacyCrtShim.cpp`. |
| `binary_function is not a member of std` | Исправлено в `Platform.h`; собираете не ту ветку. |
| `Cannot open include file: 'steam/steam_api.h'` | `SteamworksSdkDir` указывает не на папку `sdk` или без `\` в конце (скрипт добавляет сам). |
| Сотни `LNK4099: PDB 'vc70.pdb' was not found` | Безвредно: у готовых библиотек нет отладочной информации. |
| Игра: «Steam support files are missing» | Нет `_coop\steam\steam_api.dll` (шаг 4). |
| Игра: `MSVCP140.dll` не найден | Установите VC++ Redistributable **x86**. |
| Linux: `relocation R_X86_64_32 ... recompile with -fPIE` | Не добавлен `-no-pie`. |
| Linux: `Could not find library ...` | Не распакован `dependencies.zip` в `HPL2/dependencies`. |

---

## English

Ready builds are on [Releases](https://github.com/bogdanspbm/amnesia-the-dark-descent-coop/releases). The tested path
is **Windows, Win32 (x86), Visual Studio 2022**; Linux builds in CI but is untested in game; macOS is experimental.

### Windows

**Prerequisites:** Visual Studio 2022 (Community or Build Tools) with **Desktop development with C++**, including
**MSVC v143 x64/x86 build tools** and a **Windows 10/11 SDK** (any 10.0.x; tested 10.0.26100). No MFC, no v100.
Git. PowerShell 5.1+. Perl is not needed on Windows.

```powershell
git clone https://github.com/bogdanspbm/amnesia-the-dark-descent-coop.git
cd amnesia-the-dark-descent-coop
.\build\build_windows.ps1                                        # without Steam (direct IP only)
.\build\build_windows.ps1 -SteamworksSdk C:\SteamworksSDK\sdk    # with Steam lobbies
```

The script finds MSBuild via `vswhere`, unpacks `HPL2\dependencies.zip` into `HPL2\dependencies\`, builds
`HPL2\core\_HPL2_2010.vcxproj` and then `amnesia\src\game\Lux.vcxproj` as `Release|Win32` with
`/p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0`. Output: **`amnesia\redist\Amnesia.exe`**.

### Steam lobbies, step by step

1. Download the **Steamworks SDK** from https://partner.steamgames.com/downloads/list (any Steam account);
   **1.65** is tested.
2. Unpack it, e.g. to `C:\SteamworksSDK\`; you need its **`sdk`** folder (`public\steam\steam_api.h`,
   `redistributable_bin\steam_api.dll/.lib`).
3. Build with `-SteamworksSdk C:\SteamworksSDK\sdk`, or pass `/p:SteamworksSdkDir=C:\SteamworksSDK\sdk\` (trailing
   backslash) to the `Lux.vcxproj` build. This defines `COOP_STEAM`, links `steam_api.lib` and delay-loads the DLL.
4. Copy **`sdk\redistributable_bin\steam_api.dll`** to **`<game>\_coop\steam\`**. Leave the game's own older
   `steam_api.dll` next to `Amnesia.exe` alone; the mod loads the one in `_coop\steam` by path first
   (`SteamDir` in `coop.cfg`).
5. **AppID:** the mod uses Valve's **Spacewar (480)** test app and sets `SteamAppId`/`SteamGameId` itself before
   `SteamAPI_InitEx`, so **no `steam_appid.txt` is needed**. Override with `SteamAppId="..."` in `coop.cfg`.
6. Steam must be running when you play.

The SDK and `steam_api.dll` are never committed to git; the DLL only goes into the release zip.

### Package and install

```powershell
.\build\package_windows.ps1 -Version 0.1.0 [-SteamDll C:\SteamworksSDK\sdk\redistributable_bin\steam_api.dll]
```

Produces `dist\amnesia-coop-<version>-windows.zip` (`Amnesia_coop.exe`, `_coop\partner\`, optional
`_coop\steam\steam_api.dll`, `coop.cfg.example`, notes). Extract it into the game folder. The exe is named
**`Amnesia_coop.exe`** so the original `Amnesia.exe` is **never overwritten**. Players need the
[VC++ 2015–2022 Redistributable x86](https://aka.ms/vs/17/release/vc_redist.x86.exe); every other DLL is already in
the game folder.

### Linux (experimental, untested in game)

See the commands in the Russian section: install `build-essential cmake perl libgl1-mesa-dev libglu1-mesa-dev
libx11-dev zlib1g-dev`, unzip the dependencies, configure with **`-no-pie`** (the prebuilt static libraries are not
PIC) and build the `Amnesia` target. 64-bit uses `lib/linux/lib64`; the binary `Amnesia.bin.x86_64` has rpath
`$ORIGIN/lib64`, so put it into the Linux game folder as `Amnesia_coop.bin.x86_64`. Perl runs `buildcounter.pl`.
Steam: `-DSTEAMWORKS_SDK_DIR=<sdk>` (untested).

### macOS (experimental)

x86_64 only (`lib/macosx64`): `-DIS_APPLE_64=ON -DCMAKE_OSX_ARCHITECTURES=x86_64`. CI tries it on an Intel runner
with `continue-on-error`.

### FBX

The FBX mesh loader is disabled (`HPL2_WITH_FBX=OFF`, removed from the VS projects): the FBX SDK 2012 it needs is no
longer available and the game does not use it. Enable with `-DHPL2_WITH_FBX=ON` and your own FBX SDK.

### Common errors

| Error | Fix |
|---|---|
| `MSB8020: build tools for v100 cannot be found` | Pass `/p:PlatformToolset=v143` (the script does). |
| `MSB8036: Windows SDK version 10.0.15063.0 was not found` | Pass `/p:WindowsTargetPlatformVersion=10.0`. |
| `afxres.h` / `std::binary_function` / `__iob_func` errors | You are building upstream sources; these are fixed in this repository. |
| `steam/steam_api.h` not found | `SteamworksSdkDir` must point at the SDK's `sdk` folder. |
| Many `LNK4099` warnings | Harmless (prebuilt libraries without PDBs). |
| "Steam support files are missing" in game | `_coop\steam\steam_api.dll` is missing. |
| `MSVCP140.dll` missing | Install the VC++ Redistributable **x86**. |
| Linux `relocation R_X86_64_32 ... -fPIE` | Add `-no-pie`. |
