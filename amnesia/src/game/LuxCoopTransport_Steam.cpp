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

#include "LuxCoopTransport_Steam.h"

#ifdef COOP_STEAM

#if defined(WIN32)
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	//windows.h macros rename functions all over the engine
	#undef PlaySound
	#undef CreateFont
	#undef GetObject
#else
	#include <stdlib.h>
#endif

#include "steam/steam_api.h"
#include "steam/isteamnetworkingmessages.h"
#include "steam/isteamnetworkingutils.h"

#endif

//-----------------------------------------------------------------------

//Lobbies are tagged with this, so the browser only lists games running the same protocol
static const char* kSteamLobbyTagKey = "amnesia_coop";
static const int kSteamMaxMessagesPerPoll = 64;
static const int kSteamChannelNum = 2;

//-----------------------------------------------------------------------

#ifdef COOP_STEAM

//////////////////////////////////////////////////////////////////////////
// STEAM CALLBACKS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

/**
 * Steam's callback objects register themselves on construction, so they live in their own class
 * that is only made once steam_api.dll is loaded and initialized.
 */
class cLuxCoopSteamCallbacks
{
public:
	cLuxCoopSteamCallbacks(cLuxCoopTransport_Steam *apOwner) : mpOwner(apOwner){}

	STEAM_CALLBACK(cLuxCoopSteamCallbacks, OnLobbyEnter, LobbyEnter_t);
	STEAM_CALLBACK(cLuxCoopSteamCallbacks, OnLobbyChatUpdate, LobbyChatUpdate_t);
	STEAM_CALLBACK(cLuxCoopSteamCallbacks, OnJoinRequested, GameLobbyJoinRequested_t);
	STEAM_CALLBACK(cLuxCoopSteamCallbacks, OnSessionRequest, SteamNetworkingMessagesSessionRequest_t);
	STEAM_CALLBACK(cLuxCoopSteamCallbacks, OnSessionFailed, SteamNetworkingMessagesSessionFailed_t);

	void OnLobbyCreated(LobbyCreated_t *apResult, bool abIOFailure)
	{
		mpOwner->OnLobbyCreated(apResult->m_ulSteamIDLobby, abIOFailure==false && apResult->m_eResult == k_EResultOK);
	}
	void OnLobbyList(LobbyMatchList_t *apResult, bool abIOFailure)
	{
		mpOwner->OnLobbyList(abIOFailure ? 0 : (int)apResult->m_nLobbiesMatching, abIOFailure==false);
	}

	CCallResult<cLuxCoopSteamCallbacks, LobbyCreated_t> mLobbyCreatedResult;
	CCallResult<cLuxCoopSteamCallbacks, LobbyMatchList_t> mLobbyListResult;

private:
	cLuxCoopTransport_Steam *mpOwner;
};

void cLuxCoopSteamCallbacks::OnLobbyEnter(LobbyEnter_t *apData)
{
	mpOwner->OnLobbyEntered(apData->m_ulSteamIDLobby, apData->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess);
}

void cLuxCoopSteamCallbacks::OnLobbyChatUpdate(LobbyChatUpdate_t *apData)
{
	bool bEntered = (apData->m_rgfChatMemberStateChange & k_EChatMemberStateChangeEntered) != 0;
	mpOwner->OnLobbyMemberChanged(apData->m_ulSteamIDLobby, apData->m_ulSteamIDUserChanged, bEntered);
}

void cLuxCoopSteamCallbacks::OnJoinRequested(GameLobbyJoinRequested_t *apData)
{
	//Picked "Join game" on a friend, or accepted an invite, in Steam while the game runs
	char sId[32];
	sprintf(sId, "%llu", (unsigned long long)apData->m_steamIDLobby.ConvertToUint64());
	mpOwner->msJoinRequest = sId;
}

void cLuxCoopSteamCallbacks::OnSessionRequest(SteamNetworkingMessagesSessionRequest_t *apData)
{
	mpOwner->OnSessionRequest(apData->m_identityRemote.GetSteamID64());
}

void cLuxCoopSteamCallbacks::OnSessionFailed(SteamNetworkingMessagesSessionFailed_t *apData)
{
	mpOwner->OnSessionFailed(apData->m_info.m_identityRemote.GetSteamID64());
}

//-----------------------------------------------------------------------

static unsigned long long ParseSteamId(const tString& asId)
{
	return strtoull(asId.c_str(), NULL, 10);
}

static tString SteamIdToString(unsigned long long alId)
{
	char sId[32];
	sprintf(sId, "%llu", alId);
	return sId;
}

#endif

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// CONSTRUCTORS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cLuxCoopTransport_Steam::cLuxCoopTransport_Steam(int alAppId, const tString& asDllDir)
{
	mlAppId = alAppId;
	msDllDir = asDllDir;
	mbAvailable = false;
	mbInitTried = false;
	mpCallbacks = NULL;

	mbHost = false;
	mbPublicLobby = false;
	mlLobby = 0;
	mlPeer = 0;
	mbPeerConnected = false;
	mbConnectedEvent = false;
	mbDisconnectedEvent = false;

	mbListingLobbies = false;
}

cLuxCoopTransport_Steam::~cLuxCoopTransport_Steam()
{
#ifdef COOP_STEAM
	if(mbAvailable)
	{
		Stop();
		delete mpCallbacks;
		mpCallbacks = NULL;
		SteamAPI_Shutdown();
		mbAvailable = false;
	}
#endif
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

bool cLuxCoopTransport_Steam::Init()
{
	if(mbInitTried) return mbAvailable;
	mbInitTried = true;

#ifndef COOP_STEAM
	msInitError = "This build of the game has no Steam support.";
	return false;
#else
	tString sAppId = cString::ToString(mlAppId);

	#if defined(WIN32)
		//The mod's steam_api.dll is newer than the one next to the game's exe; load it by path first so the
		//delay-loaded imports bind to it instead of the game's copy
		tString sDll = msDllDir + "/steam_api.dll";
		if(LoadLibraryA(sDll.c_str())==NULL)
		{
			msInitError = "Steam support files are missing (" + sDll + ").";
			Warning("Coop: %s\n", msInitError.c_str());
			return false;
		}
		SetEnvironmentVariableA("SteamAppId", sAppId.c_str());
		SetEnvironmentVariableA("SteamGameId", sAppId.c_str());
	#else
		setenv("SteamAppId", sAppId.c_str(), 1);
		setenv("SteamGameId", sAppId.c_str(), 1);
	#endif

	SteamErrMsg sErr;
	if(SteamAPI_InitEx(&sErr) != k_ESteamAPIInitResult_OK)
	{
		msInitError = tString("Steam is not available: ") + sErr + " Is Steam running?";
		Warning("Coop: %s\n", msInitError.c_str());
		return false;
	}

	mpCallbacks = new cLuxCoopSteamCallbacks(this);
	SteamNetworkingUtils()->InitRelayNetworkAccess();
	mbAvailable = true;
	Log("Coop: Steam ready as '%s', AppID %d\n", SteamFriends()->GetPersonaName(), mlAppId);
	return true;
#endif
}

tString cLuxCoopTransport_Steam::GetPersonaName()
{
#ifdef COOP_STEAM
	if(mbAvailable) return SteamFriends()->GetPersonaName();
#endif
	return "";
}

//-----------------------------------------------------------------------

void cLuxCoopTransport_Steam::RunCallbacks()
{
#ifdef COOP_STEAM
	if(mbAvailable) SteamAPI_RunCallbacks();
#endif
}

//-----------------------------------------------------------------------

bool cLuxCoopTransport_Steam::StartHost()
{
#ifdef COOP_STEAM
	if(Init()==false) return false;
	Stop();

	mbHost = true;
	msStatus = "Creating a Steam lobby...";
	SteamAPICall_t hCall = SteamMatchmaking()->CreateLobby(mbPublicLobby ? k_ELobbyTypePublic : k_ELobbyTypeFriendsOnly, 2);
	mpCallbacks->mLobbyCreatedResult.Set(hCall, mpCallbacks, &cLuxCoopSteamCallbacks::OnLobbyCreated);
	return true;
#else
	return false;
#endif
}

bool cLuxCoopTransport_Steam::StartClient(const tString& asTarget)
{
#ifdef COOP_STEAM
	if(Init()==false) return false;
	Stop();

	unsigned long long lLobby = ParseSteamId(asTarget);
	if(lLobby==0) return false;

	mbHost = false;
	msStatus = "Joining the Steam lobby...";
	SteamMatchmaking()->JoinLobby(CSteamID((uint64)lLobby));
	return true;
#else
	return false;
#endif
}

//-----------------------------------------------------------------------

void cLuxCoopTransport_Steam::Stop()
{
#ifdef COOP_STEAM
	if(mbAvailable && mlPeer != 0)
	{
		SteamNetworkingIdentity identity;
		identity.SetSteamID64(mlPeer);
		SteamNetworkingMessages()->CloseSessionWithUser(identity);
	}
#endif
	LeaveLobby();
	mlPeer = 0;
	mbPeerConnected = false;
	mbConnectedEvent = false;
	mbDisconnectedEvent = false;
	msStatus = "";
}

//-----------------------------------------------------------------------

void cLuxCoopTransport_Steam::Update(float afTimeStep, iLuxCoopTransportListener *apListener)
{
#ifdef COOP_STEAM
	if(mbAvailable==false) return;

	if(mbConnectedEvent)
	{
		mbConnectedEvent = false;
		apListener->OnTransportConnected();
	}

	for(int lChannel=0; lChannel<kSteamChannelNum; ++lChannel)
	{
		SteamNetworkingMessage_t *vMessages[kSteamMaxMessagesPerPoll];
		int lCount = SteamNetworkingMessages()->ReceiveMessagesOnChannel(lChannel, vMessages, kSteamMaxMessagesPerPoll);
		for(int i=0; i<lCount; ++i)
		{
			SteamNetworkingMessage_t *pMessage = vMessages[i];
			//Only the partner is listened to
			if(mbPeerConnected && pMessage->m_identityPeer.GetSteamID64() == mlPeer)
				apListener->OnTransportPacket((const unsigned char*)pMessage->m_pData, (size_t)pMessage->m_cbSize);
			pMessage->Release();
		}
	}

	if(mbDisconnectedEvent)
	{
		mbDisconnectedEvent = false;
		apListener->OnTransportDisconnected();
	}
#endif
}

//-----------------------------------------------------------------------

void cLuxCoopTransport_Steam::Send(const unsigned char *apData, size_t alSize, int alChannel, bool abReliable)
{
#ifdef COOP_STEAM
	if(mbAvailable==false || HasPeer()==false || alSize==0) return;

	SteamNetworkingIdentity identity;
	identity.SetSteamID64(mlPeer);
	int lFlags = (abReliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_UnreliableNoNagle) | k_nSteamNetworkingSend_AutoRestartBrokenSession;
	SteamNetworkingMessages()->SendMessageToUser(identity, apData, (uint32)alSize, lFlags, alChannel);
#endif
}

void cLuxCoopTransport_Steam::DisconnectPeer()
{
	//Leaving the lobby is what tells the other side
	if(mbHost==false) Stop();
}

//-----------------------------------------------------------------------

tString cLuxCoopTransport_Steam::GetStatus()
{
	if(mbAvailable==false) return msInitError;
	return msStatus;
}

//-----------------------------------------------------------------------

void cLuxCoopTransport_Steam::RequestLobbyList()
{
#ifdef COOP_STEAM
	if(Init()==false || mbListingLobbies) return;

	mbListingLobbies = true;
	mvLobbies.clear();

	//Friends first: Steam knows which lobby each friend playing this AppID is in
	int lFriendFlags = k_EFriendFlagImmediate;
	int lFriendNum = SteamFriends()->GetFriendCount(lFriendFlags);
	for(int i=0; i<lFriendNum; ++i)
	{
		CSteamID friendId = SteamFriends()->GetFriendByIndex(i, lFriendFlags);
		FriendGameInfo_t gameInfo;
		if(SteamFriends()->GetFriendGamePlayed(friendId, &gameInfo)==false) continue;
		if(gameInfo.m_gameID.AppID() != (AppId_t)mlAppId || gameInfo.m_steamIDLobby.IsValid()==false) continue;

		cLuxCoopLobbyInfo info;
		info.msId = SteamIdToString(gameInfo.m_steamIDLobby.ConvertToUint64());
		info.msName = tString(SteamFriends()->GetFriendPersonaName(friendId)) + " (friend)";
		info.mlMembers = 1;
		info.mlMaxMembers = 2;
		info.mbFriend = true;
		mvLobbies.push_back(info);
	}

	SteamMatchmaking()->AddRequestLobbyListStringFilter(kSteamLobbyTagKey, cString::ToString(kCoopProtocolVersion).c_str(), k_ELobbyComparisonEqual);
	SteamMatchmaking()->AddRequestLobbyListFilterSlotsAvailable(1);
	SteamMatchmaking()->AddRequestLobbyListDistanceFilter(k_ELobbyDistanceFilterWorldwide);
	SteamAPICall_t hCall = SteamMatchmaking()->RequestLobbyList();
	mpCallbacks->mLobbyListResult.Set(hCall, mpCallbacks, &cLuxCoopSteamCallbacks::OnLobbyList);
#endif
}

void cLuxCoopTransport_Steam::OpenInviteDialog()
{
#ifdef COOP_STEAM
	if(mbAvailable && mlLobby != 0) SteamFriends()->ActivateGameOverlayInviteDialog(CSteamID((uint64)mlLobby));
#endif
}

tString cLuxCoopTransport_Steam::TakeJoinRequest()
{
	tString sRequest = msJoinRequest;
	msJoinRequest = "";
	return sRequest;
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

void cLuxCoopTransport_Steam::LeaveLobby()
{
#ifdef COOP_STEAM
	if(mbAvailable && mlLobby != 0) SteamMatchmaking()->LeaveLobby(CSteamID((uint64)mlLobby));
#endif
	mlLobby = 0;
}

//-----------------------------------------------------------------------

void cLuxCoopTransport_Steam::OnLobbyCreated(unsigned long long alLobby, bool abOk)
{
#ifdef COOP_STEAM
	if(mbHost==false) return;
	if(abOk==false)
	{
		msStatus = "Steam could not create a lobby. Check that you are online in Steam.";
		Warning("Coop: %s\n", msStatus.c_str());
		return;
	}

	mlLobby = alLobby;
	CSteamID lobbyId((uint64)alLobby);
	SteamMatchmaking()->SetLobbyData(lobbyId, kSteamLobbyTagKey, cString::ToString(kCoopProtocolVersion).c_str());
	SteamMatchmaking()->SetLobbyData(lobbyId, "name", (tString(SteamFriends()->GetPersonaName()) + "'s game").c_str());
	SteamMatchmaking()->SetLobbyJoinable(lobbyId, true);

	msStatus = mbPublicLobby ?	"Public Steam lobby open. Waiting for someone to join, or press \"Invite friend\"." :
								"Friends-only Steam lobby open. Your friends can join from \"Find games\", or press \"Invite friend\".";
	Log("Coop: Steam lobby %s created\n", SteamIdToString(alLobby).c_str());
#endif
}

//-----------------------------------------------------------------------

void cLuxCoopTransport_Steam::OnLobbyEntered(unsigned long long alLobby, bool abOk)
{
#ifdef COOP_STEAM
	if(mbHost) return;		//The host's own entry right after creating it
	if(abOk==false)
	{
		msStatus = "Could not join that lobby. It may be full or closed.";
		return;
	}

	mlLobby = alLobby;
	CSteamID lobbyId((uint64)alLobby);

	tString sVersion = SteamMatchmaking()->GetLobbyData(lobbyId, kSteamLobbyTagKey);
	if(sVersion != cString::ToString(kCoopProtocolVersion))
	{
		msStatus = "That lobby runs a different version of the co-op mod.";
		LeaveLobby();
		return;
	}

	CSteamID ownerId = SteamMatchmaking()->GetLobbyOwner(lobbyId);
	if(ownerId == SteamUser()->GetSteamID())
	{
		msStatus = "You are alone in that lobby.";
		return;
	}

	msStatus = "Joined " + tString(SteamFriends()->GetFriendPersonaName(ownerId)) + "'s lobby.";
	SetPeer(ownerId.ConvertToUint64());
#endif
}

//-----------------------------------------------------------------------

void cLuxCoopTransport_Steam::OnLobbyMemberChanged(unsigned long long alLobby, unsigned long long alMember, bool abEntered)
{
#ifdef COOP_STEAM
	if(alLobby != mlLobby || alMember == SteamUser()->GetSteamID().ConvertToUint64()) return;

	if(abEntered)
	{
		//Somebody joined the host's lobby: they are the partner
		if(mbHost && mlPeer==0)
		{
			msStatus = tString(SteamFriends()->GetFriendPersonaName(CSteamID((uint64)alMember))) + " joined the lobby.";
			SetPeer(alMember);
		}
		return;
	}

	if(alMember != mlPeer) return;

	//The partner left: the host keeps the lobby open for someone else, the client leaves the empty lobby
	Log("Coop: Steam partner left the lobby\n");
	if(mbPeerConnected) mbDisconnectedEvent = true;
	mlPeer = 0;
	mbPeerConnected = false;
	if(mbHost)	msStatus = "Your partner left. The lobby is still open.";
	else		{ msStatus = "The host closed the lobby."; LeaveLobby(); }
#endif
}

//-----------------------------------------------------------------------

void cLuxCoopTransport_Steam::OnLobbyList(int alCount, bool abOk)
{
#ifdef COOP_STEAM
	mbListingLobbies = false;
	if(abOk==false) return;

	for(int i=0; i<alCount; ++i)
	{
		CSteamID lobbyId = SteamMatchmaking()->GetLobbyByIndex(i);
		tString sId = SteamIdToString(lobbyId.ConvertToUint64());

		//Friends' lobbies are already listed
		bool bListed = false;
		for(size_t j=0; j<mvLobbies.size(); ++j) if(mvLobbies[j].msId == sId) bListed = true;
		if(bListed) continue;

		cLuxCoopLobbyInfo info;
		info.msId = sId;
		info.msName = SteamMatchmaking()->GetLobbyData(lobbyId, "name");
		if(info.msName == "") info.msName = "Co-op game";
		info.mlMembers = SteamMatchmaking()->GetNumLobbyMembers(lobbyId);
		info.mlMaxMembers = SteamMatchmaking()->GetLobbyMemberLimit(lobbyId);
		info.mbFriend = false;
		mvLobbies.push_back(info);
	}
#endif
}

//-----------------------------------------------------------------------

void cLuxCoopTransport_Steam::OnSessionRequest(unsigned long long alRemote)
{
#ifdef COOP_STEAM
	//The request can arrive before Steam says the sender entered the lobby; any lobby member may become the partner
	if(mbHost && mlPeer==0 && mlLobby != 0)
	{
		CSteamID lobbyId((uint64)mlLobby);
		int lMemberNum = SteamMatchmaking()->GetNumLobbyMembers(lobbyId);
		for(int i=0; i<lMemberNum; ++i)
		{
			if(SteamMatchmaking()->GetLobbyMemberByIndex(lobbyId, i).ConvertToUint64() != alRemote) continue;
			msStatus = tString(SteamFriends()->GetFriendPersonaName(CSteamID((uint64)alRemote))) + " joined the lobby.";
			SetPeer(alRemote);
			break;
		}
	}

	//Only the lobby partner gets through
	if(alRemote != mlPeer) return;

	SteamNetworkingIdentity identity;
	identity.SetSteamID64(alRemote);
	SteamNetworkingMessages()->AcceptSessionWithUser(identity);
#endif
}

void cLuxCoopTransport_Steam::OnSessionFailed(unsigned long long alRemote)
{
	if(alRemote != mlPeer || mbPeerConnected==false) return;

	Log("Coop: Steam connection to the partner failed\n");
	msStatus = "Lost the connection to your partner.";
	mbPeerConnected = false;
	mbDisconnectedEvent = true;
	if(mbHost==false) LeaveLobby();
	mlPeer = 0;
}

//-----------------------------------------------------------------------

void cLuxCoopTransport_Steam::SetPeer(unsigned long long alPeer)
{
	mlPeer = alPeer;
	mbPeerConnected = true;
	mbConnectedEvent = true;
}

//-----------------------------------------------------------------------
