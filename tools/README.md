# Development tools

None of these are needed to play. They expect the mod's layout inside a game install
(`<game>\_coop\dev`, `<game>\_coop\scenes`, `<game>\_coop\partner`, ...).

| Folder | What |
|---|---|
| `coop_dev/` | `coop_dev.ps1` starts a host and a client of `Amnesia_coop.exe` side by side over localhost, straight into a map or a bot scene (`-Scene`), optionally recording it (`-Record`). `record_all.ps1` records every scene, `stills_sheet.ps1` makes a contact sheet. Copy to `<game>\_coop\dev\`. The scripts also need start-up configs (`init_A.cfg`/`init_B.cfg`) derived from the game's own `config\` files, which are not in this repository. |
| `../coop_data/scenes/` | Bot scenes: XML scripts the co-op bot plays (walk, look, crouch, flee from a monster, run game script). Copy to `<game>\_coop\scenes\`. |
| `partner_build/` | Blender scripts that build the partner model in `coop_data/partner/` from the CC0 Quaternius packs (`blender -b --factory-startup --python build_partner.py`; edit the paths at the top). |
| `montage/` | A small Media Foundation tool that cuts recorded scenes into a montage (`build.bat` with MSVC). |
