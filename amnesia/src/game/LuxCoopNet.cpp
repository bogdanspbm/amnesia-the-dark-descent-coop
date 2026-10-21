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
#include "LuxPlayerState_Interact.h"
#include "LuxMainMenu.h"
#include "LuxProp_Item.h"
#include "LuxProp_Lamp.h"
#include "LuxProp_Button.h"
#include "LuxInventory.h"
#include "LuxMessageHandler.h"
#include "LuxHelpFuncs.h"

#include <enet/enet.h>
#include <string.h>
#include <stdlib.h>

//////////////////////////////////////////////////////////////////////////
// CONSTANTS
//////////////////////////////////////////////////////////////////////////

static const int kCoopProtocolVersion = 2;
static const int kCoopChannel_Reliable = 0;
static const int kCoopChannel_State = 1;
static const int kCoopChannelNum = 2;

static const unsigned char kCoopStateFlag_Lantern = 0x01;
static const unsigned char kCoopStateFlag_Crouch = 0x02;
static const unsigned char kCoopStateFlag_Dead = 0x04;

static const float kCoopBodySendInterval = 1.0f / 20.0f;
static const float kCoopBodyResendTime = 2.0f;			//Resend still bodies now and then in case a packet was lost
static const int kCoopMaxBodiesPerPacket = 14;			//~57 bytes plus the name each, keeps packets under the usual MTU
static const int kCoopMaxBodiesPerEntity = 32;
static const float kCoopOwnershipLinger = 1.0f;		//Client keeps authority this long after letting go, so throws land on its side

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

void cLuxCoopAvatar::Create(cLuxMap *apMap, const tString& asMeshFile)
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

	//The partner needs a real body so it blocks the local player, pushes props and triggers "Player" areas
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
	msAddress = "127.0.0.1";
	mlPort = 27960;
	msName = "Player";
	msAvatarMesh = "servant_grunt.dae";
	mfStateSendInterval = 1.0f / 20.0f;

	mpHost = NULL;
	mpPeer = NULL;
	mbEnetInitialized = false;
	mbHandshakeDone = false;
	mfStateSendCount = 0;
	mfReconnectCount = 0;
	mfBodySendCount = 0;
	mfTime = 0;
	mbSleepOverridden = false;
	mbSleepWhenOutOfFocus = true;
	mbApplyingRemote = false;
	mlDropCount = 0;
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
	cConfigFile *pCfg = hplNew(cConfigFile, (GetConfigFile()));
	if(pCfg->Load()==false)
	{
		Log("Coop: no coop.cfg found, using lobby defaults\n");
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

void cLuxCoopNet::SaveMainConfig()
{
	//Keep the user's own setting in the config, not the co-op override
	if(mbSleepOverridden) gpBase->mpMainConfig->SetBool("Engine","SleepWhenOutOfFocus", mbSleepWhenOutOfFocus);
}

//-----------------------------------------------------------------------

iPhysicsBody* cLuxCoopNet::GetPartnerCollideBody()
{
	if(IsConnected()==false || mAvatar.IsShown()==false) return NULL;

	iCharacterBody *pBody = mAvatar.GetCharacterBody();
	return pBody ? pBody->GetCurrentBody() : NULL;
}

//-----------------------------------------------------------------------

cVector3f cLuxCoopNet::AdjustStartPos(const cVector3f& avFeetPos, float afYaw)
{
	//Both games use the same start node, so the client steps aside instead of spawning inside the host
	if(mMode != eLuxCoopMode_Client) return avFeetPos;

	iCharacterBody *pBody = gpBase->mpPlayer->GetCharacterBody();
	if(pBody==NULL) return avFeetPos;

	cVector3f vRight = cMath::MatrixMul(cMath::MatrixRotateY(afYaw), cVector3f(1,0,0));
	cVector3f vFwd = cMath::MatrixMul(cMath::MatrixRotateY(afYaw), cVector3f(0,0,1));
	const float fDist = 1.1f;
	cVector3f vOffsets[] = { vRight*fDist, vRight*-fDist, vFwd*fDist, vFwd*-fDist, (vRight+vFwd)*fDist, (vRight-vFwd)*fDist };

	for(size_t i=0; i<sizeof(vOffsets)/sizeof(vOffsets[0]); ++i)
	{
		cVector3f vPos = avFeetPos + vOffsets[i];
		if(pBody->CheckCharacterFits(vPos, true, -1, NULL, 0.01f)) return vPos;
	}
	return avFeetPos;
}

//-----------------------------------------------------------------------

void cLuxCoopNet::OnLocalChangeMap(const tString& asMapName, const tString& asStartPos, const tString& asStartSound, const tString& asEndSound)
{
	if(IsConnected()==false || mbApplyingRemote) return;

	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_ChangeMap);
	buffer.AddString(asMapName);
	buffer.AddString(asStartPos);
	buffer.AddString(asStartSound);
	buffer.AddString(asEndSound);
	Send(buffer, kCoopChannel_Reliable, true);
}

//-----------------------------------------------------------------------

bool cLuxCoopNet::IsSharedItemType(eLuxItemType aType)
{
	return	aType == eLuxItemType_Puzzle || aType == eLuxItemType_Note || aType == eLuxItemType_Diary ||
			aType == eLuxItemType_Lantern || aType == eLuxItemType_HandObject;
}

//-----------------------------------------------------------------------

void cLuxCoopNet::OnLocalPickup(cLuxProp_Item *apItem)
{
	if(IsConnected()==false || mbApplyingRemote) return;

	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_Pickup);
	buffer.AddString(GetCurrentMapFile());
	buffer.AddString(apItem->GetName());
	Send(buffer, kCoopChannel_Reliable, true);
}

void cLuxCoopNet::OnLocalInteract(iLuxEntity *apEntity)
{
	if(IsConnected()==false || mbApplyingRemote) return;

	//Pickups and level doors have their own messages
	if(apEntity->GetEntityType() == eLuxEntityType_Prop)
	{
		eLuxPropType propType = static_cast<iLuxProp*>(apEntity)->GetPropType();
		if(propType == eLuxPropType_Item || propType == eLuxPropType_LevelDoor) return;
	}

	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_Interact);
	buffer.AddString(GetCurrentMapFile());
	buffer.AddString(apEntity->GetName());
	Send(buffer, kCoopChannel_Reliable, true);
}

void cLuxCoopNet::OnLocalLampLit(cLuxProp_Lamp *apLamp)
{
	if(IsConnected()==false || mbApplyingRemote) return;

	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_LampLit);
	buffer.AddString(GetCurrentMapFile());
	buffer.AddString(apLamp->GetName());
	Send(buffer, kCoopChannel_Reliable, true);
}

void cLuxCoopNet::OnLocalUseItem(const tString& asItem, const tString& asEntity)
{
	if(IsConnected()==false || mbApplyingRemote) return;

	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_UseItem);
	buffer.AddString(GetCurrentMapFile());
	buffer.AddString(asItem);
	buffer.AddString(asEntity);
	Send(buffer, kCoopChannel_Reliable, true);
}

void cLuxCoopNet::OnLocalCombine(const tString& asItemA, const tString& asItemB)
{
	if(IsConnected()==false || mbApplyingRemote) return;

	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_Combine);
	buffer.AddString(asItemA);
	buffer.AddString(asItemB);
	Send(buffer, kCoopChannel_Reliable, true);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::GiveOrDropItem(cLuxInventory_Item *apItem)
{
	cLuxInventory *pInventory = gpBase->mpInventory;
	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(IsConnected()==false || pMap==NULL) return;

	if(IsSharedItemType(apItem->GetType()))
	{
		pInventory->SetMessageText(_W("Story items are shared: your partner has this one too."), 0);
		return;
	}

	////////////////////////
	// Partner close by: hand it over
	cVector3f vFeetPos = gpBase->mpPlayer->GetCharacterBody()->GetFeetPosition();
	if(mAvatar.IsShown() && cMath::Vector3Dist(mAvatar.GetPosition(), vFeetPos) < 3.0f)
	{
		cLuxCoopBuffer buffer;
		buffer.AddByte(eLuxCoopMsg_GiveItem);
		buffer.AddInt(apItem->GetType());
		buffer.AddString(apItem->GetSubType());
		buffer.AddString(cString::GetFileName(apItem->GetImageName()));
		buffer.AddFloat(apItem->GetAmount());
		buffer.AddString(apItem->GetStringVal());
		buffer.AddString(apItem->GetExtraStringVal());
		buffer.AddString(apItem->GetEntityFile());
		Send(buffer, kCoopChannel_Reliable, true);

		pInventory->SetMessageText(_W("Given to ") + cString::To16Char(msPartnerName) + _W(": ") + GetItemDisplayName(apItem->GetType(), apItem->GetSubType()), 0);
		pInventory->RemoveItem(apItem);
		return;
	}

	////////////////////////
	// Otherwise drop it in front of the player, in both games
	if(apItem->GetEntityFile() == "")
	{
		pInventory->SetMessageText(_W("This item can't be dropped. Stand next to your partner and right click to hand it over."), 0);
		return;
	}

	cCamera *pCam = gpBase->mpPlayer->GetCamera();
	cVector3f vPos = pCam->GetPosition() + pCam->GetForward()*0.6f - cVector3f(0, 0.3f, 0);
	tString sName = tString("coop_drop_") + (IsHost() ? "h" : "c") + cString::ToString(++mlDropCount) + "_" + cString::ToString((int)(mfTime*1000.0f));
	tString sFile = apItem->GetEntityFile();

	cMatrixf mtxTransform = cMatrixf::Identity;
	mtxTransform.SetTranslation(vPos);
	pMap->CreateEntity(sName, sFile, mtxTransform, 1);

	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_DropItem);
	buffer.AddString(GetCurrentMapFile());
	buffer.AddString(sName);
	buffer.AddString(sFile);
	buffer.AddVector3f(vPos);
	Send(buffer, kCoopChannel_Reliable, true);

	pInventory->SetMessageText(_W("Dropped: ") + GetItemDisplayName(apItem->GetType(), apItem->GetSubType()), 0);
	pInventory->RemoveItem(apItem);
}

//-----------------------------------------------------------------------

bool cLuxCoopNet::Host(const tString& asName, int alPort)
{
	StopNetwork();
	msName = asName;
	mlPort = alPort;
	mMode = eLuxCoopMode_Host;
	SaveLobbySettings();
	return StartNetwork();
}

bool cLuxCoopNet::Join(const tString& asName, const tString& asAddress, int alPort)
{
	StopNetwork();
	msName = asName;
	msAddress = asAddress;
	mlPort = alPort;
	mMode = eLuxCoopMode_Client;
	SaveLobbySettings();
	return StartNetwork();
}

void cLuxCoopNet::Leave()
{
	StopNetwork();
	mMode = eLuxCoopMode_Off;
	msPartnerName = "";
	msPartnerMap = "";
	mAvatar.SetTarget(0, 0, false, false);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::StartCoopGame()
{
	if(IsHost()==false || IsConnected()==false) return;

	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_StartGame);
	buffer.AddByte(gpBase->mbHardMode ? 1 : 0);
	Send(buffer, kCoopChannel_Reliable, true);
	enet_host_flush(mpHost);

	StartLocalGame();
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

		mfBodySendCount -= afTimeStep;
		if(mfBodySendCount <= 0)
		{
			mfBodySendCount = kCoopBodySendInterval;
			if(IsHost())	SendBodyStates();
			else			SendOwnedBodyStates();
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
	m_mapClientOwnedBodies.clear();
	mAvatar.Create(apMap, msAvatarMesh);
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

	mAvatar.Create(apMap, msAvatarMesh);
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

	//An unfocused window would sleep without updating, so the partner times out
	if(mbSleepOverridden==false)
	{
		mbSleepWhenOutOfFocus = gpBase->mpEngine->GetWaitIfAppOutOfFocus();
		gpBase->mpEngine->SetWaitIfAppOutOfFocus(false);
		mbSleepOverridden = true;
	}

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
	if(mbSleepOverridden)
	{
		gpBase->mpEngine->SetWaitIfAppOutOfFocus(mbSleepWhenOutOfFocus);
		mbSleepOverridden = false;
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
	case eLuxCoopMsg_BodyStates:	if(mbHandshakeDone) HandleBodyStates(buffer); break;
	case eLuxCoopMsg_StartGame:		if(mbHandshakeDone && IsHost()==false) HandleStartGame(buffer); break;
	case eLuxCoopMsg_ChangeMap:		if(mbHandshakeDone) HandleChangeMap(buffer); break;
	case eLuxCoopMsg_Pickup:		if(mbHandshakeDone) HandlePickup(buffer); break;
	case eLuxCoopMsg_Interact:		if(mbHandshakeDone) HandleInteract(buffer); break;
	case eLuxCoopMsg_LampLit:		if(mbHandshakeDone) HandleLampLit(buffer); break;
	case eLuxCoopMsg_UseItem:		if(mbHandshakeDone) HandleUseItem(buffer); break;
	case eLuxCoopMsg_Combine:		if(mbHandshakeDone) HandleCombine(buffer); break;
	case eLuxCoopMsg_GiveItem:		if(mbHandshakeDone) HandleGiveItem(buffer); break;
	case eLuxCoopMsg_DropItem:		if(mbHandshakeDone) HandleDropItem(buffer); break;
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
		tString sEntity = aBuffer.GetString();
		int lBodyIdx = aBuffer.GetByte();
		cVector3f vPos = aBuffer.GetVector3f();
		cQuaternion qRot = aBuffer.GetQuaternion();
		cVector3f vLinVel = aBuffer.GetVector3f();
		cVector3f vAngVel = aBuffer.GetVector3f();
		if(aBuffer.HasReadError()) return;

		iLuxProp *pProp = static_cast<iLuxProp*>(pMap->GetEntityByName(sEntity, eLuxEntityType_Prop));
		if(pProp==NULL || lBodyIdx >= pProp->GetBodyNum()) continue;

		//The client is authoritative for bodies it holds: the host takes its states, the client ignores the host's
		tLuxCoopBodyKey lKey(sEntity, lBodyIdx);
		if(IsHost())					m_mapClientOwnedBodies[lKey] = mfTime;
		else if(IsClientOwned(lKey))	continue;

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

void cLuxCoopNet::HandleStartGame(cLuxCoopBuffer &aBuffer)
{
	bool bHardMode = aBuffer.GetByte() != 0;
	if(aBuffer.HasReadError()) return;

	if(gpBase->mpMapHandler->MapIsLoaded())
	{
		Warning("Coop: host started a game but we are already in one, ignoring\n");
		return;
	}

	Log("Coop: host started the game\n");
	gpBase->mbHardMode = bHardMode;
	StartLocalGame();
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandleChangeMap(cLuxCoopBuffer &aBuffer)
{
	tString sMap = aBuffer.GetString();
	tString sStartPos = aBuffer.GetString();
	tString sStartSound = aBuffer.GetString();
	tString sEndSound = aBuffer.GetString();
	if(aBuffer.HasReadError()) return;

	cLuxMapHandler *pMapHandler = gpBase->mpMapHandler;
	if(pMapHandler->MapIsLoaded()==false) return;

	//Both games often run the same script at once; then each is already on its way
	tString sMapFile = cString::ToLowerCase(cString::SetFileExt(sMap, "map"));
	if(pMapHandler->IsChangingMap() && cString::ToLowerCase(cString::GetFileName(pMapHandler->GetChangeMapFile())) == cString::GetFileName(sMapFile)) return;
	if(pMapHandler->IsChangingMap()==false && GetCurrentMapFile() == cString::GetFileName(sMapFile)) return;

	Log("Coop: partner went to map '%s', following\n", sMap.c_str());
	mbApplyingRemote = true;
	pMapHandler->ChangeMap(sMap, sStartPos, sStartSound, sEndSound);
	mbApplyingRemote = false;
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandlePickup(cLuxCoopBuffer &aBuffer)
{
	tString sMap = aBuffer.GetString();
	tString sName = aBuffer.GetString();
	if(aBuffer.HasReadError()) return;

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap==NULL || sMap != GetCurrentMapFile()) return;

	iLuxEntity *pEntity = pMap->GetEntityByName(sName, eLuxEntityType_Prop);
	if(pEntity==NULL || static_cast<iLuxProp*>(pEntity)->GetPropType() != eLuxPropType_Item) return;
	cLuxProp_Item *pItem = static_cast<cLuxProp_Item*>(pEntity);

	mbApplyingRemote = true;

	if(IsSharedItemType(pItem->GetItemType()))
	{
		cLuxInventory_Item *pInvItem = gpBase->mpInventory->AddItem(pItem->GetName(), pItem->GetItemType(), pItem->GetSubItemTypeName(), pItem->GetImageFile(),
																	pItem->GetAmount(), pItem->GetStringVal(), pItem->GetExtraStringVal(), NULL);
		if(pInvItem) pInvItem->SetEntityFile(pItem->GetFileName());

		ShowMessage(cString::To16Char(msPartnerName) + _W(" found: ") + GetItemDisplayName(pItem->GetItemType(), pItem->GetSubItemTypeName()));
	}

	//The scripts react to the pickup in both games
	pItem->RunCallbackFunc("OnPickup");
	pItem->RunInteractCallbackFunc();
	pMap->DestroyEntity(pItem);

	mbApplyingRemote = false;
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandleInteract(cLuxCoopBuffer &aBuffer)
{
	tString sMap = aBuffer.GetString();
	tString sName = aBuffer.GetString();
	if(aBuffer.HasReadError()) return;

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap==NULL || sMap != GetCurrentMapFile()) return;

	iLuxEntity *pEntity = pMap->GetEntityByName(sName);
	if(pEntity==NULL) return;

	mbApplyingRemote = true;

	//Buttons only flip a state; everything you grab is already moved by the body sync
	if(pEntity->GetEntityType() == eLuxEntityType_Prop && static_cast<iLuxProp*>(pEntity)->GetPropType() == eLuxPropType_Button)
	{
		pEntity->OnInteract(NULL, 0);
	}
	pEntity->RunInteractCallbackFunc();

	mbApplyingRemote = false;
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandleLampLit(cLuxCoopBuffer &aBuffer)
{
	tString sMap = aBuffer.GetString();
	tString sName = aBuffer.GetString();
	if(aBuffer.HasReadError()) return;

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap==NULL || sMap != GetCurrentMapFile()) return;

	iLuxEntity *pEntity = pMap->GetEntityByName(sName, eLuxEntityType_Prop);
	if(pEntity==NULL || static_cast<iLuxProp*>(pEntity)->GetPropType() != eLuxPropType_Lamp) return;
	cLuxProp_Lamp *pLamp = static_cast<cLuxProp_Lamp*>(pEntity);

	mbApplyingRemote = true;
	pLamp->RunCallbackFunc("OnIgnite");
	pLamp->SetLit(true, true);
	mbApplyingRemote = false;
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandleUseItem(cLuxCoopBuffer &aBuffer)
{
	tString sMap = aBuffer.GetString();
	tString sItem = aBuffer.GetString();
	tString sEntity = aBuffer.GetString();
	if(aBuffer.HasReadError()) return;

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap==NULL || sMap != GetCurrentMapFile()) return;

	cLuxUseItemCallback *pCallback = pMap->GetUseItemCallback(sItem, sEntity);
	if(pCallback==NULL) return;

	mbApplyingRemote = true;
	bool bAutoDestroy = pCallback->mbAutoDestroy;
	tString sName = pCallback->msName;
	pMap->RunScript(pCallback->msFunction+ "(\"" + pCallback->msItem + "\", \"" + pCallback->msEntity + "\")" );
	if(bAutoDestroy) pMap->RemoveUseItemCallback(pCallback, sName);
	mbApplyingRemote = false;
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandleCombine(cLuxCoopBuffer &aBuffer)
{
	tString sItemA = aBuffer.GetString();
	tString sItemB = aBuffer.GetString();
	if(aBuffer.HasReadError()) return;

	cLuxCombineItemsCallback *pComb = gpBase->mpInventory->GetCombineCallback(sItemA, sItemB);
	if(pComb==NULL) return;

	mbApplyingRemote = true;
	gpBase->mpInventory->RunCombineCallback(pComb);
	mbApplyingRemote = false;
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandleGiveItem(cLuxCoopBuffer &aBuffer)
{
	eLuxItemType type = (eLuxItemType)aBuffer.GetInt();
	tString sSubType = aBuffer.GetString();
	tString sImage = aBuffer.GetString();
	float fAmount = aBuffer.GetFloat();
	tString sVal = aBuffer.GetString();
	tString sExtraVal = aBuffer.GetString();
	tString sFile = aBuffer.GetString();
	if(aBuffer.HasReadError() || type < 0 || type >= eLuxItemType_LastEnum) return;

	mbApplyingRemote = true;
	tString sName = "coop_given_" + sSubType + "_" + cString::ToString(++mlDropCount);
	cLuxInventory_Item *pInvItem = gpBase->mpInventory->AddItem(sName, type, sSubType, sImage, fAmount, sVal, sExtraVal, NULL);
	if(pInvItem) pInvItem->SetEntityFile(sFile);
	mbApplyingRemote = false;

	gpBase->mpHelpFuncs->PlayGuiSoundData("ui_use_oil", eSoundEntryType_Gui);
	ShowMessage(cString::To16Char(msPartnerName) + _W(" gave you: ") + GetItemDisplayName(type, sSubType));
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandleDropItem(cLuxCoopBuffer &aBuffer)
{
	tString sMap = aBuffer.GetString();
	tString sName = aBuffer.GetString();
	tString sFile = aBuffer.GetString();
	cVector3f vPos = aBuffer.GetVector3f();
	if(aBuffer.HasReadError()) return;

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap==NULL || sMap != GetCurrentMapFile() || pMap->GetEntityByName(sName) != NULL) return;

	cMatrixf mtxTransform = cMatrixf::Identity;
	mtxTransform.SetTranslation(vPos);
	pMap->CreateEntity(sName, sFile, mtxTransform, 1);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::ShowMessage(const tWString& asText)
{
	gpBase->mpMessageHandler->SetMessage(asText, 0);
}

tWString cLuxCoopNet::GetItemDisplayName(eLuxItemType aType, const tString& asSubType)
{
	if(aType == eLuxItemType_Note)	return _W("a note");
	if(aType == eLuxItemType_Diary)	return _W("a diary");
	return kTranslate("Inventory", "ItemName_"+asSubType);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::StartLocalGame()
{
	gpBase->SetCustomStory(NULL);
	gpBase->mpMainMenu->SetWindowActive(eLuxMainMenuWindow_LastEnum);
	gpBase->mpMainMenu->ExitMenu(eLuxMainMenuExit_StartGame);
}

//-----------------------------------------------------------------------

tWString cLuxCoopNet::GetConfigFile()
{
	//coop.cfg sits next to the exe so both players can edit it without touching the game configs.
	//AMNESIA_COOP_CFG picks another file, eg to run host and client on one PC for testing.
	const char *pCfgEnv = getenv("AMNESIA_COOP_CFG");
	return (pCfgEnv && pCfgEnv[0]) ? cString::To16Char(pCfgEnv) : _W("coop.cfg");
}

void cLuxCoopNet::SaveLobbySettings()
{
	//Remember what was typed in the lobby; Mode is left alone so the game does not auto-connect next start
	cConfigFile *pCfg = hplNew(cConfigFile, (GetConfigFile()));
	pCfg->Load();
	pCfg->SetString("Player", "Name", msName);
	pCfg->SetString("Network", "Address", msAddress);
	pCfg->SetInt("Network", "Port", mlPort);
	pCfg->Save();
	hplDelete(pCfg);
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

			tLuxCoopBodyKey lKey(pProp->GetName(), i);
			if(IsClientOwned(lKey)) continue;

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

			AddBodyEntry(entries, pProp, i);
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

void cLuxCoopNet::SendOwnedBodyStates()
{
	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap==NULL) return;

	iPhysicsBody *pInteractBody = GetInteractBody();
	iLuxEntity *pEntity = pInteractBody ? (iLuxEntity*)pInteractBody->GetUserData() : NULL;
	if(pEntity && pEntity->GetEntityType() == eLuxEntityType_Prop)
	{
		iLuxProp *pProp = static_cast<iLuxProp*>(pEntity);
		for(int i=0; i<pProp->GetBodyNum() && i<kCoopMaxBodiesPerEntity; ++i)
		{
			if(pProp->GetBody(i) == pInteractBody)
				m_mapClientOwnedBodies[tLuxCoopBodyKey(pProp->GetName(), i)] = mfTime;
		}
	}

	cLuxCoopBuffer entries;
	int lCount = 0;
	tLuxCoopOwnedBodyMap::iterator it = m_mapClientOwnedBodies.begin();
	while(it != m_mapClientOwnedBodies.end())
	{
		if(mfTime - it->second > kCoopOwnershipLinger)
		{
			m_mapClientOwnedBodies.erase(it++);
			continue;
		}

		tString sEntity = it->first.first;
		int lBodyIdx = it->first.second;
		++it;

		iLuxProp *pProp = static_cast<iLuxProp*>(pMap->GetEntityByName(sEntity, eLuxEntityType_Prop));
		if(pProp==NULL || lBodyIdx >= pProp->GetBodyNum()) continue;

		AddBodyEntry(entries, pProp, lBodyIdx);
		++lCount;
		if(lCount == kCoopMaxBodiesPerPacket)
		{
			SendBodyPacket(GetCurrentMapFile(), entries, lCount);
			entries = cLuxCoopBuffer();
			lCount = 0;
		}
	}

	if(lCount > 0) SendBodyPacket(GetCurrentMapFile(), entries, lCount);
}

//-----------------------------------------------------------------------

bool cLuxCoopNet::IsClientOwned(const tLuxCoopBodyKey& aKey)
{
	tLuxCoopOwnedBodyMap::iterator it = m_mapClientOwnedBodies.find(aKey);
	return it != m_mapClientOwnedBodies.end() && mfTime - it->second <= kCoopOwnershipLinger;
}

//-----------------------------------------------------------------------

void cLuxCoopNet::AddBodyEntry(cLuxCoopBuffer &aBuffer, iLuxProp *apProp, int alBodyIdx)
{
	iPhysicsBody *pBody = apProp->GetBody(alBodyIdx);
	cMatrixf &mtxBody = pBody->GetLocalMatrix();
	cQuaternion qRot;
	qRot.FromRotationMatrix(mtxBody.GetRotation());

	aBuffer.AddString(apProp->GetName());
	aBuffer.AddByte((unsigned char)alBodyIdx);
	aBuffer.AddVector3f(mtxBody.GetTranslation());
	aBuffer.AddQuaternion(qRot);
	aBuffer.AddVector3f(pBody->GetLinearVelocity());
	aBuffer.AddVector3f(pBody->GetAngularVelocity());
}

//-----------------------------------------------------------------------

iPhysicsBody* cLuxCoopNet::GetInteractBody()
{
	eLuxPlayerState state = gpBase->mpPlayer->GetCurrentState();
	if(state < eLuxPlayerState_InteractGrab || state > eLuxPlayerState_InteractSlide) return NULL;

	iLuxPlayerState_Interact *pInteract = static_cast<iLuxPlayerState_Interact*>(gpBase->mpPlayer->GetCurrentStateData());
	return pInteract->GetCurrentBody();
}

//-----------------------------------------------------------------------

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
