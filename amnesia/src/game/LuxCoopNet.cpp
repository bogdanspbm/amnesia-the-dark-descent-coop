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

#include "LuxCoopNet.h"

#include "LuxMapHandler.h"
#include "LuxMap.h"
#include "LuxPlayer.h"
#include "LuxPlayerHelpers.h"
#include "LuxMoveState_Normal.h"
#include "LuxProp.h"

#include <enet/enet.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
// CONSTANTS
//////////////////////////////////////////////////////////////////////////

static const int kCoopProtocolVersion = 1;
static const int kCoopChannel_Reliable = 0;
static const int kCoopChannel_State = 1;
static const int kCoopChannelNum = 2;

static const unsigned char kCoopStateFlag_Lantern = 0x01;
static const unsigned char kCoopStateFlag_Crouch = 0x02;
static const unsigned char kCoopStateFlag_Dead = 0x04;

static const float kCoopBodySendInterval = 1.0f / 20.0f;
static const float kCoopBodyResendTime = 2.0f;			//Resend still bodies now and then in case a packet was lost
static const int kCoopMaxBodiesPerPacket = 20;			//57 bytes each, keeps packets under the usual MTU
static const int kCoopMaxBodiesPerEntity = 32;

//////////////////////////////////////////////////////////////////////////
// BUFFER
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

void cLuxCoopBuffer::AddInt(int alX)
{
	unsigned int lX = (unsigned int)alX;
	for(int i=0; i<4; ++i) mvData.push_back((unsigned char)((lX >> (i*8)) & 0xff));
}

void cLuxCoopBuffer::AddFloat(float afX)
{
	int lX;
	memcpy(&lX, &afX, sizeof(lX));
	AddInt(lX);
}

void cLuxCoopBuffer::AddString(const tString& asX)
{
	AddInt((int)asX.size());
	mvData.insert(mvData.end(), asX.begin(), asX.end());
}

//-----------------------------------------------------------------------

bool cLuxCoopBuffer::CanRead(size_t alBytes)
{
	if(mbReadError || mlReadPos + alBytes > mvData.size())
	{
		mbReadError = true;
		return false;
	}
	return true;
}

unsigned char cLuxCoopBuffer::GetByte()
{
	if(CanRead(1)==false) return 0;
	return mvData[mlReadPos++];
}

int cLuxCoopBuffer::GetInt()
{
	if(CanRead(4)==false) return 0;
	unsigned int lX = 0;
	for(int i=0; i<4; ++i) lX |= ((unsigned int)mvData[mlReadPos++]) << (i*8);
	return (int)lX;
}

float cLuxCoopBuffer::GetFloat()
{
	int lX = GetInt();
	float fX;
	memcpy(&fX, &lX, sizeof(fX));
	return fX;
}

tString cLuxCoopBuffer::GetString()
{
	int lSize = GetInt();
	if(lSize < 0 || CanRead((size_t)lSize)==false) return "";
	tString sX(mvData.begin() + mlReadPos, mvData.begin() + mlReadPos + lSize);
	mlReadPos += lSize;
	return sX;
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// AVATAR
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cLuxCoopAvatar::cLuxCoopAvatar()
{
	mpMesh = NULL;
	mpLight = NULL;
	mpBody = NULL;
	mvPos = 0;
	mvTargetPos = 0;
	mfYaw = 0;
	mfTargetYaw = 0;
	mbLanternOn = false;
	mbVisible = false;
	mbHasState = false;
}

//-----------------------------------------------------------------------

void cLuxCoopAvatar::Create(cLuxMap *apMap, const tString& asMeshFile, bool abPhysicalBody)
{
	if(apMap==NULL || mpMesh) return;

	cWorld *pWorld = apMap->GetWorld();
	cMesh *pMesh = gpBase->mpEngine->GetResources()->GetMeshManager()->CreateMesh(asMeshFile);
	if(pMesh==NULL)
	{
		Error("Coop: could not load partner mesh '%s'\n", asMeshFile.c_str());
		return;
	}

	mpMesh = pWorld->CreateMeshEntity("coop_partner_mesh", pMesh, false);

	mpLight = pWorld->CreateLightPoint("coop_partner_lantern", "", false);
	mpLight->SetDiffuseColor(cColor(0.85f, 0.7f, 0.45f, 1.0f));
	mpLight->SetRadius(5.0f);
	mpLight->SetCastShadows(false);

	//On the host the partner needs a real body so it pushes props, blocks doors and collides with the host
	if(abPhysicalBody)
	{
		cVector3f vSize = gpBase->mpGameCfg->GetVector3f("Player_Body","Size",cVector3f(0.8f, 1.8f, 0.8f));
		mpBody = apMap->GetPhysicsWorld()->CreateCharacterBody("CoopPartner", vSize);
		mpBody->SetMass(gpBase->mpGameCfg->GetFloat("Player_Body","Mass",70));
		mpBody->SetMaxPushMass(gpBase->mpGameCfg->GetFloat("Player_Body","MaxPushMass",0));
		mpBody->SetPushForce(gpBase->mpGameCfg->GetFloat("Player_Body","PushForce",0));
		mpBody->SetMaxStepSize(gpBase->mpGameCfg->GetFloat("Player_Body","MaxStepSize",0));
		mpBody->SetGravityActive(false);		//Height comes from the partner, gravity would fight it
		mpBody->SetActive(false);
	}

	mbHasState = false;
	mpMesh->SetVisible(false);
	mpLight->SetVisible(false);
}

//-----------------------------------------------------------------------

void cLuxCoopAvatar::Destroy(cLuxMap *apMap)
{
	if(apMap)
	{
		cWorld *pWorld = apMap->GetWorld();
		if(mpMesh) pWorld->DestroyMeshEntity(mpMesh);
		if(mpLight) pWorld->DestroyLight(mpLight);
		if(mpBody) apMap->GetPhysicsWorld()->DestroyCharacterBody(mpBody);
	}
	OnWorldDestroyed();
}

void cLuxCoopAvatar::OnWorldDestroyed()
{
	mpMesh = NULL;
	mpLight = NULL;
	mpBody = NULL;
	mbHasState = false;
}

//-----------------------------------------------------------------------

void cLuxCoopAvatar::SetTarget(const cVector3f& avFeetPos, float afYaw, bool abLanternOn, bool abInMap)
{
	mvTargetPos = avFeetPos;
	mfTargetYaw = afYaw;
	mbLanternOn = abLanternOn;
	mbVisible = abInMap;

	//Snap on the first state or after a teleport, otherwise the partner slides across the map
	if(mbHasState==false || cMath::Vector3DistSqr(mvPos, mvTargetPos) > 4.0f*4.0f)
	{
		mvPos = mvTargetPos;
		mfYaw = mfTargetYaw;
		if(mpBody) mpBody->SetFeetPosition(mvTargetPos);
	}
	mbHasState = true;
}

//-----------------------------------------------------------------------

void cLuxCoopAvatar::Update(float afTimeStep)
{
	if(mpMesh==NULL) return;

	bool bShow = mbHasState && mbVisible;
	mpMesh->SetVisible(bShow);
	mpLight->SetVisible(bShow && mbLanternOn);
	if(mpBody) mpBody->SetActive(bShow);
	if(bShow==false) return;

	//Steer the body with a velocity instead of placing it, so it pushes what it walks into
	if(mpBody)
	{
		cVector3f vToTarget = mvTargetPos - mpBody->GetFeetPosition();
		mpBody->SetForceVelocity(vToTarget * 10.0f);
	}

	//States arrive at ~20 Hz, so close most of the gap within about one packet
	float fT = cMath::Min(1.0f, afTimeStep * 15.0f);
	mvPos += (mvTargetPos - mvPos) * fT;
	mfYaw = cMath::TurnAngleRad(mfYaw, mfTargetYaw, k2Pif * afTimeStep);

	ApplyTransform();
}

//-----------------------------------------------------------------------

void cLuxCoopAvatar::ApplyTransform()
{
	cMatrixf mtxTransform = cMath::MatrixRotateY(mfYaw);
	mtxTransform.SetTranslation(mvPos);
	mpMesh->SetMatrix(mtxTransform);

	mpLight->SetPosition(mvPos + cVector3f(0, 1.4f, 0));
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// CONSTRUCTORS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cLuxCoopNet::cLuxCoopNet() : iLuxUpdateable("LuxCoopNet")
{
	mMode = eLuxCoopMode_Off;
	mlPort = 27960;
	mfStateSendInterval = 1.0f / 20.0f;

	mpHost = NULL;
	mpPeer = NULL;
	mbEnetInitialized = false;
	mbHandshakeDone = false;
	mfStateSendCount = 0;
	mfReconnectCount = 0;
	mfBodySendCount = 0;
	mfTime = 0;
}

//-----------------------------------------------------------------------

cLuxCoopNet::~cLuxCoopNet()
{
	StopNetwork();
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

void cLuxCoopNet::LoadMainConfig()
{
	//coop.cfg sits next to the exe so both players can edit it without touching the game configs
	cConfigFile *pCfg = hplNew(cConfigFile, (_W("coop.cfg")));
	if(pCfg->Load()==false)
	{
		Log("Coop: no coop.cfg found, co-op is off\n");
		hplDelete(pCfg);
		return;
	}

	tString sMode = cString::ToLowerCase(pCfg->GetString("Network", "Mode", "off"));
	if(sMode == "host")			mMode = eLuxCoopMode_Host;
	else if(sMode == "client")	mMode = eLuxCoopMode_Client;
	else						mMode = eLuxCoopMode_Off;

	msAddress = pCfg->GetString("Network", "Address", "127.0.0.1");
	mlPort = pCfg->GetInt("Network", "Port", 27960);
	msName = pCfg->GetString("Player", "Name", "Player");
	msAvatarMesh = pCfg->GetString("Player", "AvatarMesh", "servant_grunt.dae");

	int lRate = cMath::Max(5, pCfg->GetInt("Network", "StateRate", 20));
	mfStateSendInterval = 1.0f / (float)lRate;

	hplDelete(pCfg);

	Log("Coop: mode '%s', address %s:%d, name '%s'\n", sMode.c_str(), msAddress.c_str(), mlPort, msName.c_str());
}

//-----------------------------------------------------------------------

void cLuxCoopNet::OnStart()
{
	if(IsActive()) StartNetwork();
}

//-----------------------------------------------------------------------

void cLuxCoopNet::Update(float afTimeStep)
{
	if(mpHost==NULL) return;

	mfTime += afTimeStep;
	PollEvents();

	//Client keeps trying to reach the host until it answers
	if(mMode == eLuxCoopMode_Client && mpPeer==NULL)
	{
		mfReconnectCount -= afTimeStep;
		if(mfReconnectCount <= 0)
		{
			mfReconnectCount = 3.0f;
			ENetAddress address;
			enet_address_set_host(&address, msAddress.c_str());
			address.port = (enet_uint16)mlPort;
			mpPeer = enet_host_connect(mpHost, &address, kCoopChannelNum, 0);
			if(mpPeer==NULL) Error("Coop: could not create connection to %s:%d\n", msAddress.c_str(), mlPort);
		}
	}

	if(IsConnected())
	{
		mfStateSendCount -= afTimeStep;
		if(mfStateSendCount <= 0)
		{
			mfStateSendCount = mfStateSendInterval;
			SendPlayerState();
		}

		if(IsHost())
		{
			mfBodySendCount -= afTimeStep;
			if(mfBodySendCount <= 0)
			{
				mfBodySendCount = kCoopBodySendInterval;
				SendBodyStates();
			}
		}
	}

	mAvatar.Update(afTimeStep);

	enet_host_flush(mpHost);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::OnQuit()
{
	StopNetwork();
}

//-----------------------------------------------------------------------

void cLuxCoopNet::OnMapEnter(cLuxMap *apMap)
{
	if(IsActive()==false) return;

	m_mapSentBodies.clear();
	mAvatar.Create(apMap, msAvatarMesh, IsHost());
	if(IsConnected()) SendMapInfo();
}

void cLuxCoopNet::OnMapLeave(cLuxMap *apMap)
{
	if(IsActive()==false) return;

	mAvatar.Destroy(apMap);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::DestroyWorldEntities(cLuxMap *apMap)
{
	//The world is about to be destroyed (eg loading a save), its entities go with it
	mAvatar.OnWorldDestroyed();
}

void cLuxCoopNet::CreateWorldEntities(cLuxMap *apMap)
{
	if(IsActive()==false) return;

	mAvatar.Create(apMap, msAvatarMesh, IsHost());
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

bool cLuxCoopNet::StartNetwork()
{
	if(enet_initialize() != 0)
	{
		Error("Coop: could not initialize ENet\n");
		mMode = eLuxCoopMode_Off;
		return false;
	}
	mbEnetInitialized = true;

	if(mMode == eLuxCoopMode_Host)
	{
		ENetAddress address;
		address.host = ENET_HOST_ANY;
		address.port = (enet_uint16)mlPort;
		mpHost = enet_host_create(&address, 1, kCoopChannelNum, 0, 0);
		if(mpHost==NULL)
		{
			Error("Coop: could not listen on port %d\n", mlPort);
			mMode = eLuxCoopMode_Off;
			return false;
		}
		Log("Coop: hosting on port %d\n", mlPort);
	}
	else
	{
		mpHost = enet_host_create(NULL, 1, kCoopChannelNum, 0, 0);
		if(mpHost==NULL)
		{
			Error("Coop: could not create client host\n");
			mMode = eLuxCoopMode_Off;
			return false;
		}
		mfReconnectCount = 0;
		Log("Coop: connecting to %s:%d\n", msAddress.c_str(), mlPort);
	}

	return true;
}

//-----------------------------------------------------------------------

void cLuxCoopNet::StopNetwork()
{
	if(mpPeer)
	{
		enet_peer_disconnect_now(mpPeer, 0);
		mpPeer = NULL;
	}
	if(mpHost)
	{
		enet_host_destroy(mpHost);
		mpHost = NULL;
	}
	if(mbEnetInitialized)
	{
		enet_deinitialize();
		mbEnetInitialized = false;
	}
	mbHandshakeDone = false;
}

//-----------------------------------------------------------------------

void cLuxCoopNet::PollEvents()
{
	ENetEvent event;
	while(mpHost && enet_host_service(mpHost, &event, 0) > 0)
	{
		switch(event.type)
		{
		case ENET_EVENT_TYPE_CONNECT:
			//Only one partner; a second connection on the host is refused
			if(mpPeer && mpPeer != event.peer)
			{
				enet_peer_disconnect(event.peer, 0);
				break;
			}
			mpPeer = event.peer;
			mbHandshakeDone = false;
			Log("Coop: connected\n");
			SendHello();
			break;

		case ENET_EVENT_TYPE_RECEIVE:
			if(event.peer == mpPeer) HandlePacket(event.packet);
			enet_packet_destroy(event.packet);
			break;

		case ENET_EVENT_TYPE_DISCONNECT:
			if(event.peer == mpPeer)
			{
				Log("Coop: partner '%s' disconnected\n", msPartnerName.c_str());
				mpPeer = NULL;
				mbHandshakeDone = false;
				mfReconnectCount = 3.0f;
				msPartnerName = "";
				msPartnerMap = "";
				mAvatar.SetTarget(0, 0, false, false);
			}
			break;

		default:
			break;
		}
	}
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandlePacket(_ENetPacket *apPacket)
{
	cLuxCoopBuffer buffer(apPacket->data, apPacket->dataLength);
	unsigned char lType = buffer.GetByte();

	switch(lType)
	{
	case eLuxCoopMsg_Hello:			HandleHello(buffer); break;
	case eLuxCoopMsg_PlayerState:	if(mbHandshakeDone) HandlePlayerState(buffer); break;
	case eLuxCoopMsg_MapInfo:		if(mbHandshakeDone) HandleMapInfo(buffer); break;
	case eLuxCoopMsg_BodyStates:	if(mbHandshakeDone && IsHost()==false) HandleBodyStates(buffer); break;
	default:
		Warning("Coop: unknown message type %d\n", lType);
	}
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandleHello(cLuxCoopBuffer &aBuffer)
{
	int lVersion = aBuffer.GetInt();
	tString sName = aBuffer.GetString();
	if(aBuffer.HasReadError()) return;

	if(lVersion != kCoopProtocolVersion)
	{
		Error("Coop: partner runs protocol %d, we run %d. Update both games to the same mod version.\n", lVersion, kCoopProtocolVersion);
		enet_peer_disconnect(mpPeer, 0);
		return;
	}

	msPartnerName = sName;
	mbHandshakeDone = true;
	m_mapSentBodies.clear();
	Log("Coop: partner '%s' joined\n", msPartnerName.c_str());

	SendMapInfo();
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandlePlayerState(cLuxCoopBuffer &aBuffer)
{
	tString sMap = aBuffer.GetString();
	cVector3f vFeetPos = aBuffer.GetVector3f();
	float fYaw = aBuffer.GetFloat();
	float fPitch = aBuffer.GetFloat();
	unsigned char lFlags = aBuffer.GetByte();
	if(aBuffer.HasReadError()) return;

	msPartnerMap = sMap;
	bool bSameMap = sMap != "" && sMap == GetCurrentMapFile();
	mAvatar.SetTarget(vFeetPos, fYaw, (lFlags & kCoopStateFlag_Lantern)!=0, bSameMap);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandleMapInfo(cLuxCoopBuffer &aBuffer)
{
	tString sMap = aBuffer.GetString();
	if(aBuffer.HasReadError()) return;

	msPartnerMap = sMap;
	if(sMap != GetCurrentMapFile())
		Log("Coop: partner is on map '%s', we are on '%s'\n", sMap.c_str(), GetCurrentMapFile().c_str());
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandleBodyStates(cLuxCoopBuffer &aBuffer)
{
	tString sMap = aBuffer.GetString();
	int lCount = aBuffer.GetInt();
	if(aBuffer.HasReadError()) return;

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap==NULL || sMap != GetCurrentMapFile()) return;

	for(int i=0; i<lCount; ++i)
	{
		int lEntityID = aBuffer.GetInt();
		int lBodyIdx = aBuffer.GetByte();
		cVector3f vPos = aBuffer.GetVector3f();
		cQuaternion qRot = aBuffer.GetQuaternion();
		cVector3f vLinVel = aBuffer.GetVector3f();
		cVector3f vAngVel = aBuffer.GetVector3f();
		if(aBuffer.HasReadError()) return;

		iLuxProp *pProp = static_cast<iLuxProp*>(pMap->GetEntityByID(lEntityID, eLuxEntityType_Prop));
		if(pProp==NULL || lBodyIdx >= pProp->GetBodyNum()) continue;

		//Velocities let the local simulation carry the body forward until the next state arrives
		iPhysicsBody *pBody = pProp->GetBody(lBodyIdx);
		cMatrixf mtxTransform = cMath::MatrixQuaternion(qRot);
		mtxTransform.SetTranslation(vPos);
		pBody->Enable();
		pBody->SetMatrix(mtxTransform);
		pBody->SetLinearVelocity(vLinVel);
		pBody->SetAngularVelocity(vAngVel);
	}
}

//-----------------------------------------------------------------------

void cLuxCoopNet::SendHello()
{
	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_Hello);
	buffer.AddInt(kCoopProtocolVersion);
	buffer.AddString(msName);
	Send(buffer, kCoopChannel_Reliable, true);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::SendPlayerState()
{
	cLuxPlayer *pPlayer = gpBase->mpPlayer;
	iCharacterBody *pBody = pPlayer->GetCharacterBody();
	tString sMap = GetCurrentMapFile();
	if(pBody==NULL || sMap=="") return;

	unsigned char lFlags = 0;
	if(pPlayer->GetHelperLantern()->IsActive()) lFlags |= kCoopStateFlag_Lantern;
	if(pPlayer->IsDead()) lFlags |= kCoopStateFlag_Dead;
	if(pPlayer->GetCurrentMoveState() == eLuxMoveState_Normal)
	{
		cLuxMoveState_Normal *pMove = static_cast<cLuxMoveState_Normal*>(pPlayer->GetCurrentMoveStateData());
		if(pMove->IsCrouching()) lFlags |= kCoopStateFlag_Crouch;
	}

	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_PlayerState);
	buffer.AddString(sMap);
	buffer.AddVector3f(pBody->GetFeetPosition());
	buffer.AddFloat(pPlayer->GetCamera()->GetYaw());
	buffer.AddFloat(pPlayer->GetCamera()->GetPitch());
	buffer.AddByte(lFlags);
	Send(buffer, kCoopChannel_State, false);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::SendMapInfo()
{
	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_MapInfo);
	buffer.AddString(GetCurrentMapFile());
	Send(buffer, kCoopChannel_Reliable, true);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::SendBodyStates()
{
	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap==NULL) return;
	tString sMap = GetCurrentMapFile();

	cLuxCoopBuffer entries;
	int lCount = 0;

	cLuxEntityIterator entityIt = pMap->GetEntityIterator();
	while(entityIt.HasNext())
	{
		iLuxEntity *pEntity = entityIt.Next();
		if(pEntity->GetEntityType() != eLuxEntityType_Prop) continue;

		iLuxProp *pProp = static_cast<iLuxProp*>(pEntity);
		int lBodyNum = cMath::Min(pProp->GetBodyNum(), kCoopMaxBodiesPerEntity);
		for(int i=0; i<lBodyNum; ++i)
		{
			iPhysicsBody *pBody = pProp->GetBody(i);
			if(pBody->GetMass() <= 0) continue;

			cMatrixf &mtxBody = pBody->GetLocalMatrix();
			cVector3f vPos = mtxBody.GetTranslation();
			cQuaternion qRot;
			qRot.FromRotationMatrix(mtxBody.GetRotation());

			int lKey = pProp->GetID() * kCoopMaxBodiesPerEntity + i;
			tLuxCoopSentBodyMap::iterator it = m_mapSentBodies.find(lKey);
			if(it != m_mapSentBodies.end())
			{
				cLuxCoopSentBody &sent = it->second;
				bool bMoved =	cMath::Vector3DistSqr(sent.mvPos, vPos) > 0.001f*0.001f ||
								cMath::Abs(cMath::QuaternionDot(sent.mqRot, qRot)) < 0.99999f;
				if(bMoved==false && mfTime - sent.mfTime < kCoopBodyResendTime) continue;
			}

			cLuxCoopSentBody &sent = m_mapSentBodies[lKey];
			sent.mvPos = vPos;
			sent.mqRot = qRot;
			sent.mfTime = mfTime;

			entries.AddInt(pProp->GetID());
			entries.AddByte((unsigned char)i);
			entries.AddVector3f(vPos);
			entries.AddQuaternion(qRot);
			entries.AddVector3f(pBody->GetLinearVelocity());
			entries.AddVector3f(pBody->GetAngularVelocity());
			++lCount;

			if(lCount == kCoopMaxBodiesPerPacket)
			{
				SendBodyPacket(sMap, entries, lCount);
				entries = cLuxCoopBuffer();
				lCount = 0;
			}
		}
	}

	if(lCount > 0) SendBodyPacket(sMap, entries, lCount);
}

void cLuxCoopNet::SendBodyPacket(const tString& asMap, const cLuxCoopBuffer &aEntries, int alCount)
{
	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_BodyStates);
	buffer.AddString(asMap);
	buffer.AddInt(alCount);
	buffer.AddBuffer(aEntries);
	Send(buffer, kCoopChannel_State, false);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::Send(const cLuxCoopBuffer &aBuffer, int alChannel, bool abReliable)
{
	if(mpPeer==NULL || aBuffer.GetSize()==0) return;

	ENetPacket *pPacket = enet_packet_create(aBuffer.GetData(), aBuffer.GetSize(), abReliable ? ENET_PACKET_FLAG_RELIABLE : 0);
	enet_peer_send(mpPeer, (enet_uint8)alChannel, pPacket);
}

//-----------------------------------------------------------------------

tString cLuxCoopNet::GetCurrentMapFile()
{
	cLuxMap *pMap = gpBase->mpMapHandler ? gpBase->mpMapHandler->GetCurrentMap() : NULL;
	if(pMap==NULL) return "";
	return cString::ToLowerCase(cString::GetFileName(pMap->GetFileName()));
}

//-----------------------------------------------------------------------
