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

#ifndef LUX_COOP_NET_H
#define LUX_COOP_NET_H

//----------------------------------------------

#include "LuxBase.h"

//----------------------------------------------

struct _ENetHost;
struct _ENetPeer;
struct _ENetPacket;

//----------------------------------------------

enum eLuxCoopMode
{
	eLuxCoopMode_Off,
	eLuxCoopMode_Host,
	eLuxCoopMode_Client,
};

enum eLuxCoopMsg
{
	eLuxCoopMsg_Hello = 1,
	eLuxCoopMsg_PlayerState,
	eLuxCoopMsg_MapInfo,
};

//----------------------------------------------

/**
 * Little endian byte buffer used for building and reading packets.
 */
class cLuxCoopBuffer
{
public:
	cLuxCoopBuffer() : mlReadPos(0), mbReadError(false){}
	cLuxCoopBuffer(const unsigned char *apData, size_t alSize) : mvData(apData, apData+alSize), mlReadPos(0), mbReadError(false){}

	void AddByte(unsigned char alX){ mvData.push_back(alX); }
	void AddInt(int alX);
	void AddFloat(float afX);
	void AddVector3f(const cVector3f& avX){ AddFloat(avX.x); AddFloat(avX.y); AddFloat(avX.z); }
	void AddString(const tString& asX);

	unsigned char GetByte();
	int GetInt();
	float GetFloat();
	cVector3f GetVector3f(){ float x = GetFloat(); float y = GetFloat(); float z = GetFloat(); return cVector3f(x,y,z); }
	tString GetString();

	bool HasReadError(){ return mbReadError; }
	const unsigned char* GetData() const { return mvData.empty() ? NULL : &mvData[0]; }
	size_t GetSize() const { return mvData.size(); }

private:
	bool CanRead(size_t alBytes);

	std::vector<unsigned char> mvData;
	size_t mlReadPos;
	bool mbReadError;
};

//----------------------------------------------

/**
 * The partner as seen in the local world: a mesh plus a lantern light,
 * moved towards the latest received state.
 */
class cLuxCoopAvatar
{
public:
	cLuxCoopAvatar();

	void Create(cLuxMap *apMap, const tString& asMeshFile);
	void Destroy(cLuxMap *apMap);
	void OnWorldDestroyed();

	void SetTarget(const cVector3f& avFeetPos, float afYaw, bool abLanternOn, bool abInMap);
	void Update(float afTimeStep);

	bool IsCreated(){ return mpMesh != NULL; }

private:
	void ApplyTransform();

	cMeshEntity *mpMesh;
	cLightPoint *mpLight;

	cVector3f mvPos;
	cVector3f mvTargetPos;
	float mfYaw;
	float mfTargetYaw;
	bool mbLanternOn;
	bool mbVisible;
	bool mbHasState;
};

//----------------------------------------------

class cLuxCoopNet : public iLuxUpdateable
{
public:
	cLuxCoopNet();
	~cLuxCoopNet();

	void LoadMainConfig();

	void OnStart();
	void Update(float afTimeStep);
	void OnQuit();

	void OnMapEnter(cLuxMap *apMap);
	void OnMapLeave(cLuxMap *apMap);
	void DestroyWorldEntities(cLuxMap *apMap);
	void CreateWorldEntities(cLuxMap *apMap);

	eLuxCoopMode GetMode(){ return mMode; }
	bool IsActive(){ return mMode != eLuxCoopMode_Off; }
	bool IsHost(){ return mMode == eLuxCoopMode_Host; }
	bool IsConnected(){ return mpPeer != NULL && mbHandshakeDone; }

	const tString& GetPartnerName(){ return msPartnerName; }
	const tString& GetPartnerMap(){ return msPartnerMap; }

private:
	bool StartNetwork();
	void StopNetwork();

	void PollEvents();
	void HandlePacket(_ENetPacket *apPacket);
	void HandleHello(cLuxCoopBuffer &aBuffer);
	void HandlePlayerState(cLuxCoopBuffer &aBuffer);
	void HandleMapInfo(cLuxCoopBuffer &aBuffer);

	void SendHello();
	void SendPlayerState();
	void SendMapInfo();
	void Send(const cLuxCoopBuffer &aBuffer, int alChannel, bool abReliable);

	tString GetCurrentMapFile();

	eLuxCoopMode mMode;
	tString msAddress;
	int mlPort;
	tString msName;
	tString msAvatarMesh;
	float mfStateSendInterval;

	_ENetHost *mpHost;
	_ENetPeer *mpPeer;
	bool mbEnetInitialized;
	bool mbHandshakeDone;
	float mfStateSendCount;
	float mfReconnectCount;

	tString msPartnerName;
	tString msPartnerMap;

	cLuxCoopAvatar mAvatar;
};

//----------------------------------------------

#endif // LUX_COOP_NET_H
