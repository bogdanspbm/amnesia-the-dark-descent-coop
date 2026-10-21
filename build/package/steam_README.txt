Steam lobbies need steam_api.dll from Valve's Steamworks SDK in this folder (the game's own copy is too old).

1. Download the Steamworks SDK: https://partner.steamgames.com/downloads/list (Steam login required).
2. Copy sdk\redistributable_bin\steam_api.dll into this folder (_coop\steam\).
3. Keep Steam running. Lobbies use Valve's Spacewar test app (AppID 480), so Steam shows you as playing "Spacewar".

Without the file the Steam buttons in the Co-op menu report "Steam support files are missing";
direct IP connections work regardless.
