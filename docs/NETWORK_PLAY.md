# Игра по сети / Network play

[Русский](#русский) · [English](#english)

---

## Русский

### 1. Что нужно

- **Своя копия Amnesia: The Dark Descent** (Steam или GOG) у **обоих** игроков. Мод не содержит файлов игры.
- Одна и та же версия мода у обоих (иначе соединение отклоняется: «partner runs protocol N, we run M»).
- **Microsoft Visual C++ 2015–2022 Redistributable (x86)** — exe собран новым компилятором. Если при запуске
  ругается на `MSVCP140.dll` / `VCRUNTIME140.dll`, поставьте `vc_redist.x86.exe` с сайта Microsoft.

### 2. Установка (у обоих игроков)

1. Скачайте `amnesia-coop-<версия>-windows.zip` со страницы [Releases](https://github.com/bogdanspbm/amnesia-the-dark-descent-coop/releases).
2. Распакуйте его **в папку игры** (рядом с `Amnesia.exe`), например
   `C:\Program Files (x86)\Steam\steamapps\common\Amnesia The Dark Descent\`.
   В архиве только новые файлы — оригинальные файлы игры не перезаписываются:

   ```
   Amnesia_coop.exe          ← запускать этот файл
   coop.cfg.example          ← пример настроек
   _coop\partner\...         ← модель напарника
   _coop\steam\README.txt    ← сюда кладётся steam_api.dll для Steam-лобби (необязательно)
   ```
3. Запускайте **`Amnesia_coop.exe`** напрямую (не через кнопку «Играть» в Steam). Обычный `Amnesia.exe` остаётся
   нетронутым, одиночная игра работает как раньше.

### 3. Меню «Co-op»

В главном меню появился пункт **Co-op**. В окне:

| Поле / кнопка | Что делает |
|---|---|
| Your name | Ваше имя, его видит напарник |
| **Steam:** Host public lobby / Host for friends | Создать Steam-лобби (публичное / только для друзей) |
| Find games | Найти лобби (список ниже, кликните по нужному) |
| Invite friend | Открыть окно приглашения Steam (оверлей) |
| **Direct connection:** Host IP | IP хоста — только для подключающегося |
| Host by IP / Join by IP | Стать хостом / подключиться по IP |
| Leave | Отключиться |
| Start game | (хост) Новая игра для обоих |
| Continue together | (хост) Продолжить вдвоём с последнего сохранения хоста |

Строка статуса внизу пишет, что происходит («Waiting for a partner...», «Partner 'Имя' joined...»).
Меню сохраняет имя, адрес и режим в `coop.cfg` рядом с exe.

### 4. Прямое подключение по IP (ENet, UDP 27960)

**Хост:**
1. Co-op → **Host by IP**. Статус: «Waiting for a partner...».
2. Скажите напарнику свой IP:
   - в одной локальной сети — локальный IP (`ipconfig` → IPv4, например `192.168.1.10`);
   - через интернет — внешний IP (например, на 2ip.ru) **и** откройте порт (см. ниже);
   - через VPN (Radmin VPN / ZeroTier / Tailscale) — IP, который показывает VPN-программа.
3. Когда напарник подключится — **Start game** или **Continue together**.

**Подключающийся:**
1. Co-op → введите IP хоста в **Host IP** → **Join by IP**.
2. Ждите, пока хост запустит игру.

**Порт и брандмауэр (только у хоста):**
- Порт по умолчанию **UDP 27960** (можно поменять в `coop.cfg`, `Port="..."`, одинаково у обоих).
- Брандмауэр Windows: при первом запуске разрешите доступ `Amnesia_coop.exe` для частных сетей. Или вручную
  (PowerShell от администратора):
  ```powershell
  New-NetFirewallRule -DisplayName "Amnesia Co-op" -Direction Inbound -Protocol UDP -LocalPort 27960 -Action Allow
  ```
- Роутер: пробросьте **UDP 27960** на локальный IP хоста (раздел «Port forwarding / Виртуальные серверы»).
  Если провайдер выдаёт «серый» IP (CGNAT), проброс не поможет — используйте VPN или Steam.

**Без проброса портов** — виртуальная локальная сеть, оба ставят одну программу и заходят в одну сеть:
- **Radmin VPN** (Windows, бесплатно): создать сеть → напарник вступает → IP вида `26.x.x.x`.
- **ZeroTier**: создать сеть на my.zerotier.com → оба вступают по Network ID → хост одобряет участника.
- **Tailscale**: оба входят в один tailnet (или хост делится машиной) → IP вида `100.x.x.x`.

Затем — как обычно: Host by IP / Join by IP с IP из VPN.

### 5. Steam-лобби (без IP и портов)

Сейчас лобби работают через тестовое приложение Valve **Spacewar (AppID 480)** — у мода нет своего AppID. Поэтому:
- у **обоих** должен быть запущен Steam (в сети);
- в Steam вы будете отображаться как играющие в «Spacewar» — это нормально;
- нужен файл **`steam_api.dll` из Steamworks SDK** (версия игры слишком старая). Он не входит в релиз:
  скачайте Steamworks SDK на https://partner.steamgames.com/downloads/list (нужен вход в Steam) и скопируйте
  `sdk\redistributable_bin\steam_api.dll` в `<папка игры>\_coop\steam\`. Без него Steam-кнопки покажут
  «Steam support files are missing», а прямое подключение по IP работает как обычно.

**Хост:** Co-op → **Host for friends** (или **Host public lobby**) → **Invite friend** → выберите друга в оверлее.
**Друг:** принимает приглашение в Steam (или «Присоединиться к игре» в списке друзей), пока `Amnesia_coop.exe`
открыт в меню, либо Co-op → **Find games** → кликает по лобби хоста.
Дальше хост жмёт **Start game** / **Continue together**.

### 6. Сохранения и «Continue together»

- Каждый игрок сохраняется в своём профиле, как в обычной игре.
- **Continue together** (хост): хост отправляет напарнику своё последнее сохранение (прогресс показан в статусе),
  оба загружаются в нём. Мир — хоста; **инвентарь и состояние** у напарника — из **его собственного** последнего
  сохранения. Полученное сохранение хоста лежит у напарника как `coop_host_save.coopsav` (в списке сохранений
  меню не появляется).
- Новая игра — **Start game**.

### 7. В игре

- Мир общий: двери, ящики, предметы физики синхронизируются; монстры работают у хоста и могут охотиться на любого.
- Если вас «убили» — вы **лежите** (downed) 60 секунд. Напарник подходит и **держит «Взаимодействие»** 3 секунды,
  чтобы поднять. Не успел — смерть.
- Напарник виден со своим фонарём, приседает/бегает, падает в сценах, где игрока кладёт на пол.

### 8. Если что-то не так

- **Лог:** `Документы\Amnesia\Main\hpl.log` (или папка вашего профиля). Строки мода начинаются с `Coop:`.
- **«partner runs protocol N, we run M»** — разные версии мода, обновите обоим.
- **Подключение висит на «Connecting...»** — порт закрыт/не проброшен, неверный IP, брандмауэр, CGNAT.
  Проверьте в одной сети или через Radmin VPN/ZeroTier.
- **«Steam is not available ... Is Steam running?»** — запустите Steam и войдите в сеть.
- **«That lobby runs a different version of the co-op mod»** — разные версии мода.
- **Нет звука, когда окно неактивно / игра «засыпает»** — мод сам выключает сон в фоне на время co-op.
- **Ошибка `MSVCP140.dll`** — поставьте VC++ Redistributable x86 (см. п. 1).

### 9. coop.cfg (для продвинутых)

Меню пишет файл само. Можно задать вручную (пример — `coop.cfg.example`); разделы — элементы верхнего уровня:

```xml
<Network Mode="client" Address="26.12.34.56" Port="27960" StateRate="20" Transport="direct" SteamAppId="480" SteamDir="_coop/steam" />
<Player Name="Daniel" AvatarModel="coop_partner.ent" AvatarDir="_coop/partner" />
```

| Атрибут | Значение |
|---|---|
| `Network/Mode` | `host`, `client` или `off` (только меню) — с `host`/`client` подключение начинается сразу при запуске |
| `Network/Address`, `Port` | IP хоста и порт (UDP, по умолчанию 27960) |
| `Network/Transport` | `direct` (по IP) или `steam` |
| `Network/StateRate` | Сколько раз в секунду отправлять позицию (по умолчанию 20, минимум 5) |
| `Player/Name` | Имя |
| `Player/AvatarModel`, `AvatarDir` | Модель напарника (.ent); без неё — `alexander.ent` из игры |

Другой файл настроек можно указать переменной окружения `AMNESIA_COOP_CFG`.

---

## English

### 1. Requirements

- **Your own copy of Amnesia: The Dark Descent** (Steam or GOG) for **both** players. The mod contains no game files.
- The same mod version on both sides (otherwise the connection is refused: "partner runs protocol N, we run M").
- **Microsoft Visual C++ 2015–2022 Redistributable (x86)**. If the game complains about `MSVCP140.dll` /
  `VCRUNTIME140.dll`, install `vc_redist.x86.exe` from Microsoft.

### 2. Install (both players)

1. Download `amnesia-coop-<version>-windows.zip` from [Releases](https://github.com/bogdanspbm/amnesia-the-dark-descent-coop/releases).
2. Extract it **into the game folder** (next to `Amnesia.exe`). It only adds files, nothing of the game is
   overwritten:
   ```
   Amnesia_coop.exe          ← run this
   coop.cfg.example
   _coop\partner\...         ← the partner model
   _coop\steam\README.txt    ← put steam_api.dll here for Steam lobbies (optional)
   ```
3. Start **`Amnesia_coop.exe`** directly (not with Steam's Play button). `Amnesia.exe` stays untouched.

### 3. The Co-op menu

The main menu gets a **Co-op** entry:

| Field / button | What it does |
|---|---|
| Your name | Your name as your partner sees it |
| **Steam:** Host public lobby / Host for friends | Create a Steam lobby |
| Find games | List lobbies; click one to join |
| Invite friend | Opens Steam's invite overlay |
| **Direct connection:** Host IP | The host's IP (joining player only) |
| Host by IP / Join by IP | Host / connect by IP |
| Leave | Disconnect |
| Start game | (host) New game for both |
| Continue together | (host) Both continue from the host's newest save |

The menu remembers name, address and mode in `coop.cfg` next to the exe.

### 4. Direct IP (ENet, UDP 27960)

**Host:** Co-op → **Host by IP**, give your partner your IP (LAN IP from `ipconfig`, public IP plus port forwarding,
or the VPN's IP), then **Start game** / **Continue together** once they are in.
**Partner:** Co-op → type the host's IP into **Host IP** → **Join by IP**, wait for the host.

**Ports (host only):** default **UDP 27960** (`Port="..."` in `coop.cfg`, same on both). Allow `Amnesia_coop.exe`
through Windows Firewall (or, as admin:
`New-NetFirewallRule -DisplayName "Amnesia Co-op" -Direction Inbound -Protocol UDP -LocalPort 27960 -Action Allow`)
and forward **UDP 27960** on the router to the host's LAN IP. Behind CGNAT forwarding does not work — use a VPN or Steam.

**No port forwarding:** join the same virtual LAN with **Radmin VPN** (`26.x.x.x`), **ZeroTier** (approve the member
on my.zerotier.com) or **Tailscale** (`100.x.x.x`), then Host/Join by IP using the VPN address.

### 5. Steam lobbies (no IPs, no ports)

Lobbies currently run under Valve's **Spacewar test app (AppID 480)**, because the mod has no AppID of its own:
- both players need Steam running and online; Steam shows you as playing "Spacewar";
- you need **`steam_api.dll` from the Steamworks SDK** (the game's own copy is too old). It is not in the release:
  download the SDK from https://partner.steamgames.com/downloads/list (Steam login required) and copy
  `sdk\redistributable_bin\steam_api.dll` to `<game>\_coop\steam\`. Without it the Steam buttons report
  "Steam support files are missing"; direct IP still works.

**Host:** Co-op → **Host for friends** (or **Host public lobby**) → **Invite friend**.
**Friend:** accept the Steam invite (or "Join game" in the friends list) while `Amnesia_coop.exe` is in the menu,
or Co-op → **Find games** → click the host's lobby. Then the host presses **Start game** / **Continue together**.

### 6. Saves and Continue together

Each player saves in their own profile as usual. **Continue together** sends the host's newest save to the partner
(progress is shown in the status line) and both load it: the world is the host's, the partner keeps the
**inventory and stats of their own newest save**. The partner stores it as `coop_host_save.coopsav`, which the
load menu does not list.

### 7. In game

Shared world (doors, drawers, physics props), monsters run on the host and hunt either player. When killed you are
**downed** for 60 s; your partner holds **Interact** next to you for 3 s to revive you.

### 8. Troubleshooting

- **Log:** `Documents\Amnesia\Main\hpl.log` (or your profile's folder); mod lines start with `Coop:`.
- **"partner runs protocol N, we run M"** / **"That lobby runs a different version"** — update both to the same release.
- **Stuck on "Connecting..."** — wrong IP, port not forwarded, firewall, or CGNAT; try LAN or Radmin VPN/ZeroTier.
- **"Steam is not available ... Is Steam running?"** — start Steam and go online.
- **`MSVCP140.dll` missing** — install the VC++ Redistributable (x86).

### 9. coop.cfg

The menu writes it; see `coop.cfg.example` and the table in the Russian section above (`Network/Mode` `host`/`client`
connects right at startup, `Transport` `direct`/`steam`, `Port`, `StateRate`, `Player/Name`, `AvatarModel`,
`AvatarDir`). The environment variable `AMNESIA_COOP_CFG` selects another config file.
