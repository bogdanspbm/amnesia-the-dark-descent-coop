/*
 * Copyright © 2009-2020 Frictional Games
 *
 * This file is part of Amnesia: The Dark Descent.
 *
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * Amnesia: The Dark Descent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef LUX_COOP_TRANSPORT_STEAM_H
#define LUX_COOP_TRANSPORT_STEAM_H

//----------------------------------------------

#include "LuxCoopTransport.h"

//----------------------------------------------

class cLuxCoopSteamCallbacks;

//----------------------------------------------

/**
 * A Steam lobby seen in the lobby browser.
 */
class cLuxCoopLobbyInfo
{
public:
	tString msId;
	tString msName;
	int mlMembers;
	int mlMaxMembers;
	bool mbFriend;		//A friend is in it
};

typedef std::vector<cLuxCoopLobbyInfo> tLuxCoopLobbyInfoVec;

//----------------------------------------------

/**
 * Steam lobbies plus Steam's relayed messages, so players find each other without IPs or open ports.
 * The host owns a two player lobby; whoever joins it is the partner.
 *
 * Built only with COOP_STEAM. It loads steam_api.dll from _coop/steam, so the game's own older copy
 * stays untouched, and runs as AppID 480 (Spacewar) unless coop.cfg says otherwise.
 */
class cLuxCoopTransport_Steam : public iLuxCoopTransport
{
friend class cLuxCoopSteamCallbacks;
public:
	cLuxCoopTransport_Steam(int alAppId, const tString& asDllDir);
	~cLuxCoopTransport_Steam();

	//Starts Steam once; false when it is not built in, the dll is missing or Steam is not running
	bool Init();
	bool IsAvailable(){ return mbAvailable; }
	const tString& GetInitError(){ return msInitError; }
	tString GetPersonaName();

	//Runs Steam's callbacks; needed even while not connected, for lobby lists and invites
	void RunCallbacks();

	void SetPublicLobby(bool abX){ mbPublicLobby = abX; }
	//The lobby is named after the name typed in the co-op menu: the menu font has no glyphs for many Steam names
	void SetLobbyOwnerName(const tString& asX){ msLobbyOwnerName = asX; }
	bool GetPublicLobby(){ return mbPublicLobby; }

	/////////////////////////
	// Transport
	bool StartHost();
	bool StartClient(const tString& asTarget);
	void Stop();

	void Update(float afTimeStep, iLuxCoopTransportListener *apListener);
	void Flush(){}

	bool HasPeer(){ return mlPeer != 0 && mbPeerConnected; }
	void Send(const unsigned char *apData, size_t alSize, int alChannel, bool abReliable);
	void DisconnectPeer();

	tString GetStatus();

	/////////////////////////
	// Lobby browser and invites
	void RequestLobbyList();
	bool IsListingLobbies(){ return mbListingLobbies; }
	const tLuxCoopLobbyInfoVec& GetLobbies(){ return mvLobbies; }
	bool InLobby(){ return mlLobby != 0; }
	void OpenInviteDialog();

	//A lobby the player chose to join from the Steam overlay or friends list, "" when none
	tString TakeJoinRequest();

private:
	void LeaveLobby();
	void OnLobbyCreated(unsigned long long alLobby, bool abOk);
	void OnLobbyEntered(unsigned long long alLobby, bool abOk);
	void OnLobbyMemberChanged(unsigned long long alLobby, unsigned long long alMember, bool abEntered);
	void OnLobbyList(int alCount, bool abOk);
	void OnSessionRequest(unsigned long long alRemote);
	void OnSessionFailed(unsigned long long alRemote);
	void SetPeer(unsigned long long alPeer);

	int mlAppId;
	tString msDllDir;
	bool mbAvailable;
	bool mbInitTried;
	tString msInitError;
	cLuxCoopSteamCallbacks *mpCallbacks;

	bool mbHost;
	bool mbPublicLobby;
	tString msLobbyOwnerName;
	unsigned long long mlLobby;
	unsigned long long mlPeer;
	bool mbPeerConnected;
	bool mbConnectedEvent;
	bool mbDisconnectedEvent;
	tString msStatus;

	bool mbListingLobbies;
	tLuxCoopLobbyInfoVec mvLobbies;
	tString msJoinRequest;
};

//----------------------------------------------

#endif // LUX_COOP_TRANSPORT_STEAM_H
