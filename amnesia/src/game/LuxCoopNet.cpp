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
#include "LuxAreaNodes.h"
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
#include "LuxEnemy.h"
#include "LuxPlayerHelpers.h"
#include "LuxCoopTransport_ENet.h"
#include "LuxCoopTransport_Steam.h"

#include <string.h>
#include <algorithm>

#include <stdlib.h>

//////////////////////////////////////////////////////////////////////////
// CONSTANTS
//////////////////////////////////////////////////////////////////////////

static const int kCoopChannel_Reliable = 0;
static const int kCoopChannel_State = 1;
static const int kCoopChannelNum = 2;

static const unsigned char kCoopStateFlag_Lantern = 0x01;
static const unsigned char kCoopStateFlag_Crouch = 0x02;
static const unsigned char kCoopStateFlag_Dead = 0x04;
static const unsigned char kCoopStateFlag_Downed = 0x08;
static const unsigned char kCoopStateFlag_Lying = 0x10;		//A cutscene has the player on the floor (head lowered or camera rolled)

static const float kCoopLyingHeadDrop = 0.6f;			//Script head offset below this counts as lying
static const float kCoopLyingRoll = 0.5f;				//So does a camera roll past ~30 degrees

static const float kCoopDownedTime = 60.0f;			//Seconds a downed player waits for help before really dying
static const float kCoopReviveTime = 3.0f;			//Seconds the partner holds Interact to revive
static const float kCoopReviveDist = 1.8f;
static const float kCoopReviveHealth = 30.0f;

static const float kCoopCheckPointCatchUpDist = 10.0f;	//How much farther from a new checkpoint than the partner before being brought over

static const char* kCoopFallbackAvatar = "alexander.ent";

static const float kCoopEnemySendInterval = 1.0f / 15.0f;
static const int kCoopMaxEnemiesPerPacket = 12;

static const float kCoopBodySendInterval = 1.0f / 20.0f;
static const int kCoopMaxBodiesPerTick = 60;			//Send budget: at 20 Hz about 40 KB/s with every body moving
static const int kCoopMaxBodiesPerPacket = 30;			//36 bytes each when moving, keeps packets under the usual MTU
static const int kCoopMaxBodiesPerEntity = 32;
static const int kCoopBodyRestSends = 2;				//Times a body that stopped is sent flagged as resting
static const float kCoopBodyAuditTime = 15.0f;			//Resting bodies are resent this rarely, in case all rest packets were lost
static const float kCoopBodyDistFalloff = 1.0f / 8.0f;	//Priority halves 8 m from the partner
static const float kCoopBodySnapDist = 0.6f;			//Farther off than this the body is placed, closer it is steered
static const float kCoopBodySnapAngle = 0.6f;
static const float kCoopBodyCorrection = 10.0f;		//1/s, how fast a steered body closes the gap
static const float kCoopBodyRestTolerance = 0.003f;
static const float kCoopNameHashRebuildTime = 0.25f;
static const float kCoopQuatCompMax = 0.70711f;		//The three smallest quaternion components are within +-1/sqrt(2)
static const float kCoopLinVelScale = 0.002f;			//m/s per unit, +-65 m/s
static const float kCoopAngVelScale = 0.004f;			//rad/s per unit, +-131 rad/s
static const unsigned char kCoopBodyFlag_LargestMask = 0x03;
static const unsigned char kCoopBodyFlag_Rest = 0x04;
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

//Exact (case insensitive) animation names a partner .ent can provide. Missing ones fall back:
//lantern variants to the plain ones, crouch to idle/walk, death to downed, and a model
//without a lying animation is tipped over instead.
static const char* gsCoopAvatarAnimNames[eLuxCoopAvatarAnim_LastEnum] =
{
	"Idle", "Walk", "Run", "CrouchIdle", "CrouchWalk",
	"IdleLantern", "WalkLantern", "RunLantern", "CrouchIdleLantern", "CrouchWalkLantern",
	"Hurt", "Downed", "Death",
};

//-----------------------------------------------------------------------

cLuxCoopAvatar::cLuxCoopAvatar()
{
	mpMesh = NULL;
	mpLight = NULL;
	mpLanternMesh = NULL;
	mpLanternBone = NULL;
	mpBody = NULL;
	for(int i=0; i<eLuxCoopAvatarAnim_LastEnum; ++i) mvAnims[i] = NULL;
	mpAnimCurrent = NULL;
	mbHasLyingAnim = false;
	mfYawOffset = 0;
	mfSpeed = 0;
	mfBobCount = 0;
	mfHurtCount = 0;
	mvLanternOffset = cVector3f(0, 1.4f, 0);
	mvPos = 0;
	mvTargetPos = 0;
	mfYaw = 0;
	mfTargetYaw = 0;
	mbLanternOn = false;
	mbDowned = false;
	mbDead = false;
	mbCrouching = false;
	mlHurtCount = -1;
	mbVisible = false;
	mbHasState = false;
}

//-----------------------------------------------------------------------

void cLuxCoopAvatar::Create(cLuxMap *apMap, const cLuxCoopAvatarSettings& aConfigSettings)
{
	if(apMap==NULL || mpMesh) return;

	cWorld *pWorld = apMap->GetWorld();
	cResources *pResources = gpBase->mpEngine->GetResources();
	cLuxCoopAvatarSettings settings = aConfigSettings;
	tString sModelFile = settings.msModel;

	//The mod's own model may not be installed; Alexander is always there
	if(pResources->GetFileSearcher()->GetFilePath(sModelFile) == _W("") && sModelFile != kCoopFallbackAvatar)
	{
		Warning("Coop: partner model '%s' not found, using '%s'\n", sModelFile.c_str(), kCoopFallbackAvatar);
		sModelFile = kCoopFallbackAvatar;
	}

	////////////////////////
	// The model is an .ent (mesh plus its animations) or a bare mesh
	tString sMeshFile = sModelFile;
	iXmlDocument *pEntDoc = NULL;
	tString sEntPath;
	if(cString::ToLowerCase(cString::GetFileExt(sModelFile)) == "ent")
	{
		sEntPath = cString::To8Char(pResources->GetFileSearcher()->GetFilePath(sModelFile));
		pEntDoc = sEntPath != "" ? pResources->LoadXmlDocument(sEntPath) : NULL;
		cXmlElement *pModelData = pEntDoc ? pEntDoc->GetFirstElement("ModelData") : NULL;
		cXmlElement *pMeshElem = pModelData ? pModelData->GetFirstElement("Mesh") : NULL;
		sMeshFile = pMeshElem ? pMeshElem->GetAttributeString("Filename") : "";
		if(sMeshFile == "")
		{
			Error("Coop: could not read the mesh of partner entity '%s'\n", sModelFile.c_str());
			if(pEntDoc) pResources->DestroyXmlDocument(pEntDoc);
			return;
		}
	}

	cMesh *pMesh = pResources->GetMeshManager()->CreateMesh(sMeshFile);
	if(pMesh==NULL)
	{
		Error("Coop: could not load partner mesh '%s'\n", sMeshFile.c_str());
		if(pEntDoc) pResources->DestroyXmlDocument(pEntDoc);
		return;
	}

	mpMesh = pWorld->CreateMeshEntity("coop_partner_mesh", pMesh, false);

	for(int i=0; i<eLuxCoopAvatarAnim_LastEnum; ++i) mvAnims[i] = NULL;
	mpAnimCurrent = NULL;
	if(pEntDoc)
	{
		cXmlElement *pModelData = pEntDoc->GetFirstElement("ModelData");
		LoadEntityAnimations(pWorld, pModelData->GetFirstElement("Animations"), sEntPath);
		ReadEntitySettings(pEntDoc->GetFirstElement("CoopAvatar"), settings);
		pResources->DestroyXmlDocument(pEntDoc);
	}
	mfYawOffset = settings.mfYawOffset;

	////////////////////////
	// Lantern: a mesh in the hand plus its light
	mpLanternBone = settings.msLanternBone != "" ? mpMesh->GetBoneStateFromName(settings.msLanternBone) : NULL;
	if(settings.msLanternBone != "" && mpLanternBone==NULL)
		Warning("Coop: partner model has no bone '%s' for the lantern\n", settings.msLanternBone.c_str());
	if(settings.msLanternOffset != "")	mvLanternOffset = cString::ToVector3f(settings.msLanternOffset.c_str(), 0);
	else								mvLanternOffset = mpLanternBone ? cVector3f(0) : cVector3f(0, 1.4f, 0);

	mpLanternMesh = NULL;
	if(settings.msLanternMesh != "" && mpLanternBone)
	{
		cMesh *pLanternMesh = pResources->GetMeshManager()->CreateMesh(settings.msLanternMesh);
		if(pLanternMesh)
		{
			mpLanternMesh = pWorld->CreateMeshEntity("coop_partner_lantern_mesh", pLanternMesh, false);
			mpLanternBone->AddEntity(mpLanternMesh);
		}
		else
		{
			Warning("Coop: could not load partner lantern mesh '%s'\n", settings.msLanternMesh.c_str());
		}
	}

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
	if(mpLanternMesh) mpLanternMesh->SetVisible(false);
}

//-----------------------------------------------------------------------

void cLuxCoopAvatar::ReadEntitySettings(cXmlElement *apElem, cLuxCoopAvatarSettings &aSettings)
{
	//A model made for the mod says how it faces and where its hand is; coop.cfg still wins when set there
	if(apElem==NULL) return;

	aSettings.mfYawOffset += cMath::ToRad(apElem->GetAttributeFloat("YawOffset", 0));
	if(aSettings.msLanternMesh == "")	aSettings.msLanternMesh = apElem->GetAttributeString("LanternMesh", "");
	if(aSettings.msLanternBone == "")	aSettings.msLanternBone = apElem->GetAttributeString("LanternBone", "");
	if(aSettings.msLanternOffset == "")	aSettings.msLanternOffset = apElem->GetAttributeString("LanternOffset", "");
}

//-----------------------------------------------------------------------

void cLuxCoopAvatar::LoadEntityAnimations(cWorld *apWorld, cXmlElement *apAnimationsElem, const tString& asEntPath)
{
	if(apAnimationsElem==NULL) return;

	cXmlNodeListIterator it = apAnimationsElem->GetChildIterator();
	while(it.HasNext())
	{
		cXmlElement *pAnimElem = it.Next()->ToElement();
		tString sName = pAnimElem->GetAttributeString("Name");
		tString sFile = pAnimElem->GetAttributeString("File");
		tString sLowName = cString::ToLowerCase(sName);

		//Exact names first (models made for the mod), then idle/walk/run anywhere in the name (the game's characters)
		int lSlot = -1;
		for(int i=0; i<eLuxCoopAvatarAnim_LastEnum && lSlot<0; ++i)
			if(mvAnims[i]==NULL && sLowName == cString::ToLowerCase(gsCoopAvatarAnimNames[i])) lSlot = i;
		if(lSlot < 0)
		{
			if(mvAnims[eLuxCoopAvatarAnim_Idle]==NULL && sLowName.find("idle") != tString::npos)		lSlot = eLuxCoopAvatarAnim_Idle;
			else if(mvAnims[eLuxCoopAvatarAnim_Walk]==NULL && sLowName.find("walk") != tString::npos)	lSlot = eLuxCoopAvatarAnim_Walk;
			else if(mvAnims[eLuxCoopAvatarAnim_Run]==NULL && sLowName.find("run") != tString::npos)	lSlot = eLuxCoopAvatarAnim_Run;
		}
		if(lSlot < 0) continue;

		//Some of the game's .ent files keep absolute paths from the developers' machines; the files sit next to the .ent
		bool bAbsolute = sFile.find(':') != tString::npos || (sFile.length() > 0 && sFile[0] == '/');
		if(bAbsolute)
		{
			size_t lRedist = cString::ToLowerCase(sFile).find("redist/");
			if(lRedist != tString::npos)	sFile = sFile.substr(lRedist + 7);
			else							sFile = cString::GetFileName(sFile);
		}
		else if(cString::GetFilePath(sFile).length() <= 1) sFile = cString::SetFilePath(sFile, cString::GetFilePath(asEntPath));

		cAnimation *pAnim = apWorld->GetResources()->GetAnimationManager()->CreateAnimation(sFile);
		if(pAnim==NULL) continue;

		cAnimationState *pState = mpMesh->AddAnimation(pAnim, sName, pAnimElem->GetAttributeFloat("Speed", 1.0f));
		pState->SetLoop(lSlot != eLuxCoopAvatarAnim_Hurt && lSlot != eLuxCoopAvatarAnim_Death);
		mvAnims[lSlot] = pState;
	}

	////////////////////////
	// Fill the gaps from what is there
	cAnimationState **pA = mvAnims;
	if(pA[eLuxCoopAvatarAnim_Run]==NULL) pA[eLuxCoopAvatarAnim_Run] = pA[eLuxCoopAvatarAnim_Walk];
	if(pA[eLuxCoopAvatarAnim_Walk]==NULL) pA[eLuxCoopAvatarAnim_Walk] = pA[eLuxCoopAvatarAnim_Run];
	if(pA[eLuxCoopAvatarAnim_CrouchIdle]==NULL) pA[eLuxCoopAvatarAnim_CrouchIdle] = pA[eLuxCoopAvatarAnim_Idle];
	if(pA[eLuxCoopAvatarAnim_CrouchWalk]==NULL) pA[eLuxCoopAvatarAnim_CrouchWalk] = pA[eLuxCoopAvatarAnim_Walk];
	for(int i=0; i<5; ++i)
		if(pA[eLuxCoopAvatarAnim_IdleLantern + i]==NULL) pA[eLuxCoopAvatarAnim_IdleLantern + i] = pA[eLuxCoopAvatarAnim_Idle + i];
	mbHasLyingAnim = pA[eLuxCoopAvatarAnim_Downed] != NULL;
	if(pA[eLuxCoopAvatarAnim_Death]==NULL) pA[eLuxCoopAvatarAnim_Death] = pA[eLuxCoopAvatarAnim_Downed];

	tString sFound;
	for(int i=0; i<eLuxCoopAvatarAnim_LastEnum; ++i)
		sFound += tString(gsCoopAvatarAnimNames[i]) + "=" + (pA[i] ? pA[i]->GetName() : "-") + " ";
	Log("Coop: partner animations %s\n", sFound.c_str());

	PlayAnimState(pA[eLuxCoopAvatarAnim_Idle]);
}

//-----------------------------------------------------------------------

void cLuxCoopAvatar::PlayAnimState(cAnimationState *apState, float afFadeTime)
{
	if(apState==NULL || apState == mpAnimCurrent) return;

	if(mpAnimCurrent)
	{
		mpAnimCurrent->FadeOut(afFadeTime);
		apState->SetActive(true);
		if(apState->IsFading()==false) apState->SetWeight(0);
		apState->FadeIn(afFadeTime);
	}
	else
	{
		apState->SetActive(true);
		apState->SetWeight(1);
	}
	mpAnimCurrent = apState;
}

cAnimationState* cLuxCoopAvatar::GetLocomotionAnim(float afSpeed)
{
	int lLantern = mbLanternOn ? eLuxCoopAvatarAnim_IdleLantern - eLuxCoopAvatarAnim_Idle : 0;

	if(mbCrouching)
	{
		if(afSpeed < 0.2f)	return mvAnims[eLuxCoopAvatarAnim_CrouchIdle + lLantern];
		return mvAnims[eLuxCoopAvatarAnim_CrouchWalk + lLantern];
	}

	if(afSpeed < 0.3f)
	{
		cAnimationState *pIdle = mvAnims[eLuxCoopAvatarAnim_Idle + lLantern];
		return pIdle ? pIdle : mvAnims[eLuxCoopAvatarAnim_Walk + lLantern];
	}
	if(afSpeed < 3.0f) return mvAnims[eLuxCoopAvatarAnim_Walk + lLantern];
	return mvAnims[eLuxCoopAvatarAnim_Run + lLantern];
}

void cLuxCoopAvatar::UpdateAnimation(float afTimeStep, float afSpeed)
{
	mfSpeed = mfSpeed*0.8f + afSpeed*0.2f;
	mfBobCount += afTimeStep * cMath::Min(mfSpeed, 4.0f) * 4.0f;

	////////////////////////
	// Lying down: death plays once and holds, downed loops
	if(mbDowned || mbDead)
	{
		cAnimationState *pLying = mvAnims[mbDead ? eLuxCoopAvatarAnim_Death : eLuxCoopAvatarAnim_Downed];
		if(pLying)
		{
			if(pLying != mpAnimCurrent && pLying->IsLooping()==false) pLying->SetTimePosition(0);
			PlayAnimState(pLying, 0.4f);
			pLying->SetSpeed(1.0f);
			return;
		}
	}

	////////////////////////
	// Hurt plays through once, then movement takes over again
	if(mfHurtCount > 0)
	{
		mfHurtCount -= afTimeStep;
		if(mfHurtCount > 0) return;
	}

	cAnimationState *pAnim = GetLocomotionAnim(mfSpeed);
	if(pAnim==NULL) return;
	PlayAnimState(pAnim);

	//Match the steps to how fast the partner moves
	bool bWalk = pAnim == mvAnims[eLuxCoopAvatarAnim_Walk] || pAnim == mvAnims[eLuxCoopAvatarAnim_WalkLantern] ||
				 pAnim == mvAnims[eLuxCoopAvatarAnim_CrouchWalk] || pAnim == mvAnims[eLuxCoopAvatarAnim_CrouchWalkLantern];
	bool bRun = pAnim == mvAnims[eLuxCoopAvatarAnim_Run] || pAnim == mvAnims[eLuxCoopAvatarAnim_RunLantern];
	if(bRun)							pAnim->SetSpeed(cMath::Clamp(mfSpeed / 4.0f, 0.6f, 1.6f));
	else if(bWalk && mfSpeed >= 0.2f)	pAnim->SetSpeed(cMath::Clamp(mfSpeed / (mbCrouching ? 1.0f : 1.6f), 0.5f, 1.8f));
	else								pAnim->SetSpeed(1.0f);
}

//-----------------------------------------------------------------------

void cLuxCoopAvatar::Destroy(cLuxMap *apMap)
{
	if(apMap)
	{
		cWorld *pWorld = apMap->GetWorld();
		if(mpLanternMesh) pWorld->DestroyMeshEntity(mpLanternMesh);
		if(mpMesh) pWorld->DestroyMeshEntity(mpMesh);
		if(mpLight) pWorld->DestroyLight(mpLight);
		if(mpBody) apMap->GetPhysicsWorld()->DestroyCharacterBody(mpBody);
	}
	OnWorldDestroyed();
}

void cLuxCoopAvatar::OnWorldDestroyed()
{
	for(int i=0; i<eLuxCoopAvatarAnim_LastEnum; ++i) mvAnims[i] = NULL;
	mpAnimCurrent = NULL;
	mpMesh = NULL;
	mpLanternMesh = NULL;
	mpLanternBone = NULL;
	mpLight = NULL;
	mpBody = NULL;
	mbHasState = false;
}

//-----------------------------------------------------------------------

void cLuxCoopAvatar::SetTarget(const cVector3f& avFeetPos, float afYaw, bool abLanternOn, bool abInMap, bool abDowned, bool abDead, bool abCrouching, int alHurtCount)
{
	mvTargetPos = avFeetPos;
	mfTargetYaw = afYaw;
	mbLanternOn = abLanternOn;
	mbDowned = abDowned;
	mbDead = abDead;
	mbCrouching = abCrouching;
	mbVisible = abInMap;

	//The partner's hurt counter went up since the last state: flinch
	if(alHurtCount >= 0)
	{
		cAnimationState *pHurt = mvAnims[eLuxCoopAvatarAnim_Hurt];
		if(mlHurtCount >= 0 && alHurtCount != mlHurtCount && pHurt && mbDowned==false && mbDead==false)
		{
			pHurt->SetTimePosition(0);
			PlayAnimState(pHurt, 0.1f);
			mfHurtCount = pHurt->GetLength() / cMath::Max(pHurt->GetSpeed(), 0.01f) - 0.2f;
		}
		mlHurtCount = alHurtCount;
	}

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
	if(mpLanternMesh) mpLanternMesh->SetVisible(bShow && mbLanternOn);
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
	cVector3f vMove = (mvTargetPos - mvPos) * fT;
	mvPos += vMove;
	vMove.y = 0;
	bool bLying = mbDowned || mbDead;
	UpdateAnimation(afTimeStep, (afTimeStep > 0 && bLying==false) ? vMove.Length() / afTimeStep : 0);
	mfYaw = cMath::TurnAngleRad(mfYaw, mfTargetYaw, k2Pif * afTimeStep);

	ApplyTransform();
}

//-----------------------------------------------------------------------

void cLuxCoopAvatar::ApplyTransform()
{
	//A downed partner lies on the floor; models without a lying animation are tipped over
	bool bTipOver = (mbDowned || mbDead) && mbHasLyingAnim==false;
	cMatrixf mtxTransform = cMath::MatrixRotateY(mfYaw + mfYawOffset);
	if(bTipOver) mtxTransform = cMath::MatrixMul(mtxTransform, cMath::MatrixRotateX(-kPi2f));
	//Models without a walk animation bob a little while moving, so they don't just glide
	bool bBob = mvAnims[eLuxCoopAvatarAnim_Walk]==NULL && bTipOver==false && mfSpeed > 0.3f;
	float fBob = bBob ? std::fabs(sin(mfBobCount)) * 0.05f : 0;
	mtxTransform.SetTranslation(mvPos + (bTipOver ? cVector3f(0, 0.25f, 0) : cVector3f(0, fBob, 0)));
	mpMesh->SetMatrix(mtxTransform);

	//The light hangs from the hand when there is a bone for it, otherwise at chest height
	if(mpLanternBone)	mpLight->SetPosition(cMath::MatrixMul(mpLanternBone->GetWorldMatrix(), mvLanternOffset));
	else				mpLight->SetPosition(mvPos + mvLanternOffset);
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
	mAvatarSettings.msModel = "coop_partner.ent";
	mAvatarSettings.msModelDir = "_coop/partner";
	mAvatarSettings.mfYawOffset = 0;
	mbAvatarDirAdded = false;
	mlHurtCount = 0;
	mfLastHealth = -1;
	mfStateSendInterval = 1.0f / 20.0f;

	mNetType = eLuxCoopNetType_Direct;
	mpTransport = NULL;
	mpENet = NULL;
	mpSteam = NULL;
	mlSteamAppId = 480;
	msSteamDir = "_coop/steam";
	mbHandshakeDone = false;
	mfStateSendCount = 0;
	mfBodySendCount = 0;
	mfPropNameHashTime = -1000.0f;
	mfTime = 0;
	mbSleepOverridden = false;
	mbSleepWhenOutOfFocus = true;
	mbApplyingRemote = false;
	mlDropCount = 0;

	mbDowned = false;
	mbForceDeath = false;
	mfDownedTime = 0;
	mfDownedMessageCount = 0;
	mfReviveCount = 0;
	mbPartnerDowned = false;
	mbPartnerDead = false;
	mbPartnerLanternOn = false;
	mbPartnerCrouching = false;
	mfPartnerLightLevel = 1;
	mfPartnerAvgSpeed = 0;
	mfPartnerLastStateTime = 0;
	mvPartnerLastPos = 0;
	mfEnemySendCount = 0;
}

//-----------------------------------------------------------------------

cLuxCoopNet::~cLuxCoopNet()
{
	StopNetwork();
	if(mpENet) hplDelete(mpENet);
	if(mpSteam) hplDelete(mpSteam);
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
	mNetType = cString::ToLowerCase(pCfg->GetString("Network", "Transport", "direct")) == "steam" ? eLuxCoopNetType_Steam : eLuxCoopNetType_Direct;
	mlSteamAppId = pCfg->GetInt("Network", "SteamAppId", mlSteamAppId);
	msSteamDir = pCfg->GetString("Network", "SteamDir", msSteamDir);
	msSteamLobby = pCfg->GetString("Network", "SteamLobby", "");
	msName = pCfg->GetString("Player", "Name", "Player");
	mAvatarSettings.msModel = pCfg->GetString("Player", "AvatarModel", mAvatarSettings.msModel);
	mAvatarSettings.msModelDir = pCfg->GetString("Player", "AvatarDir", mAvatarSettings.msModelDir);
	mAvatarSettings.mfYawOffset = cMath::ToRad(pCfg->GetFloat("Player", "AvatarYawOffset", 0));
	mAvatarSettings.msLanternMesh = pCfg->GetString("Player", "AvatarLanternMesh", "");
	mAvatarSettings.msLanternBone = pCfg->GetString("Player", "AvatarLanternBone", "");
	mAvatarSettings.msLanternOffset = pCfg->GetString("Player", "AvatarLanternOffset", "");

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

void cLuxCoopNet::OnCheckPoint(cLuxNode_PlayerStart *apStartNode)
{
	if(apStartNode==NULL || IsConnected()==false || mAvatar.IsShown()==false) return;
	if(mbDowned || mbPartnerDowned || mbPartnerDead) return;

	//Both games run the checkpoint script, and only the one farther away moves, so the two never swap places
	iCharacterBody *pBody = gpBase->mpPlayer->GetCharacterBody();
	const cVector3f& vCheckPos = apStartNode->GetPosition();
	float fLocalDist = cMath::Vector3Dist(pBody->GetFeetPosition(), vCheckPos);
	float fPartnerDist = cMath::Vector3Dist(mAvatar.GetPosition(), vCheckPos);
	if(fLocalDist < fPartnerDist + kCoopCheckPointCatchUpDist) return;

	cVector3f vPartnerPos = mAvatar.GetPosition();
	const float fDist = 1.1f;
	cVector3f vOffsets[] = { cVector3f(fDist,0,0), cVector3f(-fDist,0,0), cVector3f(0,0,fDist), cVector3f(0,0,-fDist), cVector3f(0) };
	for(size_t i=0; i<sizeof(vOffsets)/sizeof(vOffsets[0]); ++i)
	{
		cVector3f vPos = vPartnerPos + vOffsets[i] + cVector3f(0, 0.05f, 0);
		if(i+1 < sizeof(vOffsets)/sizeof(vOffsets[0]) && pBody->CheckCharacterFits(vPos, true, -1, NULL, 0.01f)==false) continue;

		Log("Coop: left behind at a checkpoint, joining partner\n");
		pBody->SetFeetPosition(vPos);
		gpBase->mpPlayer->GetHelperHudEffect()->Flash(cColor(0,1), eGuiMaterial_Alpha, 0, 0.5f);
		ShowMessage(_W("You caught up with ") + cString::To16Char(msPartnerName));
		return;
	}
}

//-----------------------------------------------------------------------

bool cLuxCoopNet::Host(const tString& asName, int alPort)
{
	StopNetwork();
	msName = asName;
	mlPort = alPort;
	mMode = eLuxCoopMode_Host;
	mNetType = eLuxCoopNetType_Direct;
	if(mpENet){ hplDelete(mpENet); mpENet = NULL; }	//The port may have changed
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
	mNetType = eLuxCoopNetType_Direct;
	if(mpENet){ hplDelete(mpENet); mpENet = NULL; }
	SaveLobbySettings();
	return StartNetwork();
}

bool cLuxCoopNet::HostSteam(const tString& asName, bool abPublic)
{
	StopNetwork();
	if(SteamAvailable()==false) return false;

	msName = asName;
	mMode = eLuxCoopMode_Host;
	mNetType = eLuxCoopNetType_Steam;
	mpSteam->SetPublicLobby(abPublic);
	SaveLobbySettings();
	return StartNetwork();
}

bool cLuxCoopNet::JoinSteamLobby(const tString& asName, const tString& asLobbyId)
{
	StopNetwork();
	if(SteamAvailable()==false) return false;

	msName = asName;
	msSteamLobby = asLobbyId;
	mMode = eLuxCoopMode_Client;
	mNetType = eLuxCoopNetType_Steam;
	SaveLobbySettings();
	return StartNetwork();
}

bool cLuxCoopNet::SteamAvailable()
{
	if(mpSteam==NULL) mpSteam = hplNew(cLuxCoopTransport_Steam, (mlSteamAppId, msSteamDir));
	return mpSteam->Init();
}

tString cLuxCoopNet::GetTransportStatus()
{
	if(mpTransport) return mpTransport->GetStatus();
	if(mpSteam && mpSteam->GetInitError() != "") return mpSteam->GetInitError();
	return "";
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
	mpTransport->Flush();

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
	//Steam runs even while not connected, for lobby lists and invites from the overlay
	if(mpSteam)
	{
		mpSteam->RunCallbacks();
		tString sJoinLobby = mpSteam->TakeJoinRequest();
		if(sJoinLobby != "" && gpBase->mpMapHandler->MapIsLoaded()==false)
			JoinSteamLobby(msName, sJoinLobby);
	}

	if(mpTransport==NULL) return;

	mfTime += afTimeStep;
	mpTransport->Update(afTimeStep, this);

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

		mfEnemySendCount -= afTimeStep;
		if(mfEnemySendCount <= 0)
		{
			mfEnemySendCount = kCoopEnemySendInterval;
			if(IsHost()) SendEnemyStates();
		}
	}

	if(gpBase->mpMapHandler->MapIsLoaded())
	{
		UpdateDowned(afTimeStep);

		//Count hits so the partner's copy of us can flinch; a counter survives a lost state packet
		float fHealth = gpBase->mpPlayer->GetHealth();
		if(mfLastHealth >= 0 && fHealth < mfLastHealth - 0.5f) ++mlHurtCount;
		mfLastHealth = fHealth;
	}

	mAvatar.Update(afTimeStep);

	if(mpTransport) mpTransport->Flush();
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
	m_mapPropNameHashes.clear();
	mfPropNameHashTime = -1000.0f;
	CreateAvatar(apMap);
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

	CreateAvatar(apMap);
}

void cLuxCoopNet::CreateAvatar(cLuxMap *apMap)
{
	//The mod's partner model lives outside the game's folders, so they are left untouched.
	//Added before the first avatar is made, whichever of map enter or world creation comes first.
	if(mbAvatarDirAdded==false && mAvatarSettings.msModelDir != "")
	{
		mbAvatarDirAdded = true;
		tWString sDir = cString::To16Char(mAvatarSettings.msModelDir);
		if(cPlatform::FolderExists(sDir)) gpBase->mpEngine->GetResources()->AddResourceDir(sDir, true);
	}
	mAvatar.Create(apMap, mAvatarSettings);
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

bool cLuxCoopNet::StartNetwork()
{
	StopNetwork();

	if(mNetType == eLuxCoopNetType_Steam)
	{
		if(SteamAvailable()==false)
		{
			mMode = eLuxCoopMode_Off;
			return false;
		}
		mpTransport = mpSteam;
	}
	else
	{
		if(mpENet==NULL) mpENet = hplNew(cLuxCoopTransport_ENet, (mlPort, kCoopChannelNum));
		mpTransport = mpENet;
	}

	bool bStarted = false;
	if(mMode == eLuxCoopMode_Host)	bStarted = mpTransport->StartHost();
	else							bStarted = mpTransport->StartClient(mNetType == eLuxCoopNetType_Steam ? msSteamLobby : msAddress);
	if(bStarted==false)
	{
		mpTransport->Stop();
		mpTransport = NULL;
		mMode = eLuxCoopMode_Off;
		return false;
	}

	//An unfocused window would sleep without updating, so the partner times out
	if(mbSleepOverridden==false)
	{
		mbSleepWhenOutOfFocus = gpBase->mpEngine->GetWaitIfAppOutOfFocus();
		gpBase->mpEngine->SetWaitIfAppOutOfFocus(false);
		mbSleepOverridden = true;
	}

	return true;
}

//-----------------------------------------------------------------------

void cLuxCoopNet::StopNetwork()
{
	if(mpTransport)
	{
		mpTransport->Stop();
		mpTransport = NULL;
	}
	if(mbSleepOverridden)
	{
		gpBase->mpEngine->SetWaitIfAppOutOfFocus(mbSleepWhenOutOfFocus);
		mbSleepOverridden = false;
	}
	mbHandshakeDone = false;
}

//-----------------------------------------------------------------------

bool cLuxCoopNet::IsConnected()
{
	return mpTransport && mpTransport->HasPeer() && mbHandshakeDone;
}

//-----------------------------------------------------------------------

void cLuxCoopNet::OnTransportConnected()
{
	mbHandshakeDone = false;
	Log("Coop: connected\n");
	SendHello();
}

void cLuxCoopNet::OnTransportDisconnected()
{
	Log("Coop: partner '%s' disconnected\n", msPartnerName.c_str());
	mbHandshakeDone = false;
	msPartnerName = "";
	msPartnerMap = "";
	mAvatar.SetTarget(0, 0, false, false);
}

void cLuxCoopNet::OnTransportPacket(const unsigned char *apData, size_t alSize)
{
	cLuxCoopBuffer buffer(apData, alSize);
	HandlePacket(buffer);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandlePacket(cLuxCoopBuffer &buffer)
{
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
	case eLuxCoopMsg_Revive:		if(mbHandshakeDone) HandleRevive(buffer); break;
	case eLuxCoopMsg_EnemyStates:	if(mbHandshakeDone && IsHost()==false) HandleEnemyStates(buffer); break;
	case eLuxCoopMsg_EnemySound:	if(mbHandshakeDone && IsHost()==false) HandleEnemySound(buffer); break;
	case eLuxCoopMsg_Damage:		if(mbHandshakeDone) HandleDamage(buffer); break;
	case eLuxCoopMsg_PropHealth:	if(mbHandshakeDone && IsHost()==false) HandlePropHealth(buffer); break;
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
		mpTransport->DisconnectPeer();
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
	float fLightLevel = aBuffer.GetFloat();
	int lHurtCount = aBuffer.GetByte();
	if(aBuffer.HasReadError()) return;

	float fDeltaTime = mfTime - mfPartnerLastStateTime;
	if(fDeltaTime > 0.001f && fDeltaTime < 1.0f)
	{
		float fSpeed = cMath::Vector3Dist(vFeetPos, mvPartnerLastPos) / fDeltaTime;
		mfPartnerAvgSpeed = mfPartnerAvgSpeed*0.7f + fSpeed*0.3f;
	}
	mfPartnerLastStateTime = mfTime;
	mvPartnerLastPos = vFeetPos;
	mfPartnerLightLevel = fLightLevel;
	mbPartnerLanternOn = (lFlags & kCoopStateFlag_Lantern)!=0;
	mbPartnerCrouching = (lFlags & kCoopStateFlag_Crouch)!=0;

	msPartnerMap = sMap;
	mbPartnerDowned = (lFlags & kCoopStateFlag_Downed)!=0;
	mbPartnerDead = (lFlags & kCoopStateFlag_Dead)!=0;
	bool bSameMap = sMap != "" && sMap == GetCurrentMapFile();
	//Lying in a cutscene looks the same as being downed
	bool bLying = mbPartnerDowned || (lFlags & kCoopStateFlag_Lying)!=0;
	mAvatar.SetTarget(vFeetPos, fYaw, mbPartnerLanternOn, bSameMap, bLying, mbPartnerDead, mbPartnerCrouching, lHurtCount);
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
	int lCount = aBuffer.GetByte();
	if(aBuffer.HasReadError()) return;

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap==NULL || sMap != GetCurrentMapFile()) return;

	for(int i=0; i<lCount; ++i)
	{
		unsigned int lHash = (unsigned int)aBuffer.GetInt();
		int lBodyIdx = aBuffer.GetByte();
		unsigned char lFlags = aBuffer.GetByte();
		cVector3f vPos = aBuffer.GetVector3f();

		int lLargest = lFlags & kCoopBodyFlag_LargestMask;
		bool bRest = (lFlags & kCoopBodyFlag_Rest) != 0;
		float vComp[4];
		float fSumSqr = 0;
		for(int j=0; j<4; ++j)
		{
			if(j == lLargest) continue;
			vComp[j] = (aBuffer.GetShort() / 32767.0f) * kCoopQuatCompMax;
			fSumSqr += vComp[j]*vComp[j];
		}
		vComp[lLargest] = sqrtf(cMath::Max(1.0f - fSumSqr, 0.0f));
		cQuaternion qRot(vComp[0], vComp[1], vComp[2], vComp[3]);
		qRot.Normalize();

		cVector3f vLinVel(0), vAngVel(0);
		if(bRest==false)
		{
			for(int j=0; j<3; ++j) vLinVel.v[j] = aBuffer.GetShort() * kCoopLinVelScale;
			for(int j=0; j<3; ++j) vAngVel.v[j] = aBuffer.GetShort() * kCoopAngVelScale;
		}
		if(aBuffer.HasReadError()) return;

		iLuxProp *pProp = GetPropByNameHash(lHash);
		if(pProp==NULL || lBodyIdx >= pProp->GetBodyNum()) continue;

		//The client is authoritative for bodies it holds: the host takes its states, the client ignores the host's
		tLuxCoopBodyKey lKey(pProp->GetName(), lBodyIdx);
		if(IsHost())					m_mapClientOwnedBodies[lKey] = mfTime;
		else if(IsClientOwned(lKey))	continue;

		ApplyBodyState(pProp->GetBody(lBodyIdx), vPos, qRot, vLinVel, vAngVel, bRest);
	}
}

//-----------------------------------------------------------------------

bool cLuxCoopNet::IgnoreLocalPropDamage()
{
	//On the client things only break when the host says so, otherwise the two games could break different things
	return IsConnected() && IsHost()==false && mbApplyingRemote==false;
}

void cLuxCoopNet::OnPropHealthChange(iLuxProp *apProp, float afOldHealth)
{
	if(IsConnected()==false || IsHost()==false || mbApplyingRemote) return;
	if(apProp->GetHealth() == afOldHealth) return;

	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_PropHealth);
	buffer.AddString(GetCurrentMapFile());
	buffer.AddString(apProp->GetName());
	buffer.AddFloat(apProp->GetHealth());
	Send(buffer, kCoopChannel_Reliable, true);
}

void cLuxCoopNet::HandlePropHealth(cLuxCoopBuffer &aBuffer)
{
	tString sMap = aBuffer.GetString();
	tString sEntity = aBuffer.GetString();
	float fHealth = aBuffer.GetFloat();
	if(aBuffer.HasReadError()) return;

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap==NULL || sMap != GetCurrentMapFile()) return;

	iLuxProp *pProp = static_cast<iLuxProp*>(pMap->GetEntityByName(sEntity, eLuxEntityType_Prop));
	if(pProp==NULL || pProp->GetHealth() == fHealth) return;

	mbApplyingRemote = true;
	pProp->SetHealth(fHealth);
	mbApplyingRemote = false;
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

bool cLuxCoopNet::TryStartDowned()
{
	//Real death when alone, when the partner is down too, or when the downed time ran out
	if(mbForceDeath || mbDowned || IsConnected()==false || mAvatar.IsShown()==false) return false;
	if(mbPartnerDowned || mbPartnerDead) return false;

	cLuxPlayer *pPlayer = gpBase->mpPlayer;
	mbDowned = true;
	mfDownedTime = kCoopDownedTime;
	mfDownedMessageCount = 0;

	pPlayer->ChangeState(eLuxPlayerState_Normal);
	pPlayer->GetHelperLantern()->SetActive(false, false, false);
	pPlayer->SetHeadPosAdd(eLuxHeadPosAdd_Death, cVector3f(0, -1.1f, 0));
	pPlayer->SetActive(false);

	Log("Coop: player is down\n");
	SendPlayerState();
	return true;
}

//-----------------------------------------------------------------------

void cLuxCoopNet::EndDowned()
{
	cLuxPlayer *pPlayer = gpBase->mpPlayer;
	mbDowned = false;
	pPlayer->SetHeadPosAdd(eLuxHeadPosAdd_Death, cVector3f(0, 0, 0));
	pPlayer->SetActive(true);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::UpdateDowned(float afTimeStep)
{
	cLuxPlayer *pPlayer = gpBase->mpPlayer;

	////////////////////////
	// Local player is down
	if(mbDowned)
	{
		mfDownedTime -= afTimeStep;
		if(mfDownedTime <= 0 || IsConnected()==false || mbPartnerDowned || mbPartnerDead)
		{
			Log("Coop: nobody came to help, player dies\n");
			EndDowned();
			mbForceDeath = true;
			pPlayer->GetHelperDeath()->Start();
			mbForceDeath = false;
			return;
		}

		mfDownedMessageCount -= afTimeStep;
		if(mfDownedMessageCount <= 0)
		{
			mfDownedMessageCount = 1.0f;
			ShowMessage(_W("You are down! ") + cString::To16Char(msPartnerName) + _W(" can revive you: ") + cString::ToStringW((int)mfDownedTime) + _W(" s"));
		}
		return;
	}

	////////////////////////
	// Partner is down: hold Interact next to them
	if(mbPartnerDowned==false || pPlayer->IsDead() || mAvatar.IsShown()==false)
	{
		mfReviveCount = 0;
		return;
	}

	float fDist = cMath::Vector3Dist(mAvatar.GetPosition(), pPlayer->GetCharacterBody()->GetFeetPosition());
	if(fDist > kCoopReviveDist)
	{
		mfReviveCount = 0;
		return;
	}

	cAction *pInteract = gpBase->mpEngine->GetInput()->GetAction(eLuxAction_Interact);
	if(pInteract && pInteract->IsTriggerd())
	{
		mfReviveCount += afTimeStep;
		if(mfReviveCount >= kCoopReviveTime)
		{
			mfReviveCount = 0;
			cLuxCoopBuffer buffer;
			buffer.AddByte(eLuxCoopMsg_Revive);
			Send(buffer, kCoopChannel_Reliable, true);
			mbPartnerDowned = false;
			ShowMessage(_W("You revived ") + cString::To16Char(msPartnerName));
		}
		else
		{
			ShowMessage(_W("Reviving ") + cString::To16Char(msPartnerName) + _W("... ") + cString::ToStringW((int)(100.0f * mfReviveCount / kCoopReviveTime)) + _W("%"));
		}
	}
	else
	{
		mfReviveCount = 0;
		ShowMessage(cString::To16Char(msPartnerName) + _W(" is down! Hold Interact next to them to revive"));
	}
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandleRevive(cLuxCoopBuffer &aBuffer)
{
	if(mbDowned==false) return;

	EndDowned();
	gpBase->mpPlayer->SetHealth(kCoopReviveHealth);
	Log("Coop: revived by partner\n");
	ShowMessage(cString::To16Char(msPartnerName) + _W(" revived you"));
	SendPlayerState();
}

//-----------------------------------------------------------------------

iCharacterBody* cLuxCoopNet::GetPartnerCharBody()
{
	if(IsHost()==false || IsConnected()==false || mAvatar.IsShown()==false) return NULL;
	return mAvatar.GetCharacterBody();
}

//-----------------------------------------------------------------------

void cLuxCoopNet::OnEnemySound(iLuxEnemy *apEnemy, const tString& asSound)
{
	if(IsHost()==false || IsConnected()==false) return;

	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_EnemySound);
	buffer.AddString(GetCurrentMapFile());
	buffer.AddString(apEnemy->GetName());
	buffer.AddString(asSound);
	Send(buffer, kCoopChannel_Reliable, true);
}

void cLuxCoopNet::DamagePartner(float afAmount, int alStrength, eLuxDamageType aType, bool abLethal)
{
	if(IsConnected()==false) return;

	cLuxCoopBuffer buffer;
	buffer.AddByte(eLuxCoopMsg_Damage);
	buffer.AddFloat(afAmount);
	buffer.AddInt(alStrength);
	buffer.AddInt(aType);
	buffer.AddByte(abLethal ? 1 : 0);
	Send(buffer, kCoopChannel_Reliable, true);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::SendEnemyStates()
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
		if(pEntity->GetEntityType() != eLuxEntityType_Enemy) continue;
		iLuxEnemy *pEnemy = static_cast<iLuxEnemy*>(pEntity);
		iCharacterBody *pBody = pEnemy->GetCharacterBody();
		if(pBody==NULL) continue;

		cAnimationState *pAnim = pEnemy->GetCurrentAnimation();

		entries.AddString(pEnemy->GetName());
		entries.AddByte(pEnemy->IsActive() ? 1 : 0);
		entries.AddVector3f(pBody->GetFeetPosition());
		entries.AddFloat(pBody->GetYaw());
		entries.AddString(pAnim ? pAnim->GetName() : "");
		entries.AddByte(pAnim && pAnim->IsLooping() ? 1 : 0);
		entries.AddFloat(pAnim ? pAnim->GetSpeed() : 1.0f);
		entries.AddInt(pEnemy->GetCurrentEnemyState());
		++lCount;

		if(lCount == kCoopMaxEnemiesPerPacket)
		{
			cLuxCoopBuffer buffer;
			buffer.AddByte(eLuxCoopMsg_EnemyStates);
			buffer.AddString(sMap);
			buffer.AddInt(lCount);
			buffer.AddBuffer(entries);
			Send(buffer, kCoopChannel_State, false);
			entries = cLuxCoopBuffer();
			lCount = 0;
		}
	}

	if(lCount > 0)
	{
		cLuxCoopBuffer buffer;
		buffer.AddByte(eLuxCoopMsg_EnemyStates);
		buffer.AddString(sMap);
		buffer.AddInt(lCount);
		buffer.AddBuffer(entries);
		Send(buffer, kCoopChannel_State, false);
	}
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandleEnemyStates(cLuxCoopBuffer &aBuffer)
{
	tString sMap = aBuffer.GetString();
	int lCount = aBuffer.GetInt();
	if(aBuffer.HasReadError()) return;

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap==NULL || sMap != GetCurrentMapFile()) return;

	for(int i=0; i<lCount; ++i)
	{
		tString sName = aBuffer.GetString();
		bool bActive = aBuffer.GetByte() != 0;
		cVector3f vFeetPos = aBuffer.GetVector3f();
		float fYaw = aBuffer.GetFloat();
		tString sAnim = aBuffer.GetString();
		bool bLoop = aBuffer.GetByte() != 0;
		float fAnimSpeed = aBuffer.GetFloat();
		int lState = aBuffer.GetInt();
		if(aBuffer.HasReadError()) return;

		iLuxEnemy *pEnemy = static_cast<iLuxEnemy*>(pMap->GetEntityByName(sName, eLuxEntityType_Enemy));
		if(pEnemy==NULL) continue;

		if(pEnemy->IsActive() != bActive) pEnemy->SetActive(bActive);
		if(bActive) pEnemy->SetCoopMirrorState(vFeetPos, fYaw, sAnim, bLoop, fAnimSpeed, lState);
	}
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandleEnemySound(cLuxCoopBuffer &aBuffer)
{
	tString sMap = aBuffer.GetString();
	tString sName = aBuffer.GetString();
	tString sSound = aBuffer.GetString();
	if(aBuffer.HasReadError()) return;

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap==NULL || sMap != GetCurrentMapFile()) return;

	iLuxEnemy *pEnemy = static_cast<iLuxEnemy*>(pMap->GetEntityByName(sName, eLuxEntityType_Enemy));
	if(pEnemy && pEnemy->IsActive()) pEnemy->PlaySound(sSound);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::HandleDamage(cLuxCoopBuffer &aBuffer)
{
	float fAmount = aBuffer.GetFloat();
	int lStrength = aBuffer.GetInt();
	int lType = aBuffer.GetInt();
	bool bLethal = aBuffer.GetByte() != 0;
	if(aBuffer.HasReadError() || lType < 0 || lType >= eLuxDamageType_LastEnum) return;

	if(gpBase->mpMapHandler->MapIsLoaded()==false) return;
	gpBase->mpPlayer->GiveDamage(fAmount, lStrength, (eLuxDamageType)lType, true, bLethal);
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
	pCfg->SetString("Network", "Transport", mNetType == eLuxCoopNetType_Steam ? "steam" : "direct");
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
	if(mbDowned) lFlags |= kCoopStateFlag_Downed;
	else if(pPlayer->GetHelperDeath()->IsActive()) lFlags |= kCoopStateFlag_Dead;
	if(mbDowned==false && (pPlayer->GetHeadPosAddSum().y < -kCoopLyingHeadDrop || cMath::Abs(pPlayer->GetRoll()) > kCoopLyingRoll))
		lFlags |= kCoopStateFlag_Lying;
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
	buffer.AddFloat(pPlayer->GetHelperLightLevel()->GetNormalLightLevel());
	buffer.AddByte((unsigned char)(mlHurtCount & 0xff));
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

	//Bodies near the partner matter most to them, so they get the bandwidth first
	cVector3f vPartnerPos = mAvatar.GetPosition();

	std::vector<cLuxCoopBodyCandidate> vCandidates;

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
			if(pBody->GetMass() <= 0 || pBody->IsActive()==false) continue;

			tLuxCoopBodyKey lKey(pProp->GetName(), i);
			if(IsClientOwned(lKey)) continue;

			cMatrixf &mtxBody = pBody->GetLocalMatrix();
			cVector3f vPos = mtxBody.GetTranslation();
			cQuaternion qRot;
			qRot.FromRotationMatrix(mtxBody.GetRotation());

			cLuxCoopSentBody &sent = m_mapSentBodies[lKey];
			bool bNew = sent.mfTime < -100.0f;
			bool bMoved =	bNew ||
							cMath::Vector3DistSqr(sent.mvPos, vPos) > 0.001f*0.001f ||
							cMath::Abs(cMath::QuaternionDot(sent.mqRot, qRot)) < 0.99999f;
			float fSinceSent = mfTime - sent.mfTime;

			bool bRest = false;
			float fWeight = 1.0f;
			if(bMoved)
			{
				//A moving body: the longer since it was sent, the more it is owed an update
				fWeight = bNew ? 1.0f : 4.0f;
				sent.mlRestSendsLeft = kCoopBodyRestSends;
				bRest = bNew && pBody->GetEnabled()==false;
			}
			else if(sent.mlRestSendsLeft > 0)
			{
				//Just stopped: send the final pose a couple of times so the partner pins it in place
				if(fSinceSent < kCoopBodySendInterval*2.0f) continue;
				bRest = true;
				fWeight = 3.0f;
			}
			else
			{
				//At rest and already pinned: only a slow audit, in case every rest packet was lost
				if(fSinceSent < kCoopBodyAuditTime) continue;
				bRest = true;
				fWeight = 0.25f;
			}

			float fDist = cMath::Vector3Dist(vPos, vPartnerPos);
			cLuxCoopBodyCandidate candidate;
			candidate.mpProp = pProp;
			candidate.mlBodyIdx = i;
			candidate.mbRest = bRest;
			candidate.mfPriority = (cMath::Min(fSinceSent, 10.0f) + kCoopBodySendInterval) * fWeight / (1.0f + fDist * kCoopBodyDistFalloff);
			vCandidates.push_back(candidate);
		}
	}

	SendBodyCandidates(vCandidates);
}

static bool CoopCandidateHigherPriority(const cLuxCoopBodyCandidate& aA, const cLuxCoopBodyCandidate& aB)
{
	return aA.mfPriority > aB.mfPriority;
}

void cLuxCoopNet::SendBodyCandidates(std::vector<cLuxCoopBodyCandidate> &avCandidates)
{
	//Only the most urgent bodies fit in this tick's budget, the rest wait and gain priority
	if((int)avCandidates.size() > kCoopMaxBodiesPerTick)
	{
		std::partial_sort(avCandidates.begin(), avCandidates.begin() + kCoopMaxBodiesPerTick, avCandidates.end(), CoopCandidateHigherPriority);
		avCandidates.resize(kCoopMaxBodiesPerTick);
	}

	tString sMap = GetCurrentMapFile();
	cLuxCoopBuffer entries;
	int lCount = 0;

	for(size_t i=0; i<avCandidates.size(); ++i)
	{
		cLuxCoopBodyCandidate &candidate = avCandidates[i];
		iPhysicsBody *pBody = candidate.mpProp->GetBody(candidate.mlBodyIdx);
		cMatrixf &mtxBody = pBody->GetLocalMatrix();

		cLuxCoopSentBody &sent = m_mapSentBodies[tLuxCoopBodyKey(candidate.mpProp->GetName(), candidate.mlBodyIdx)];
		sent.mvPos = mtxBody.GetTranslation();
		sent.mqRot.FromRotationMatrix(mtxBody.GetRotation());
		sent.mfTime = mfTime;
		if(candidate.mbRest && sent.mlRestSendsLeft > 0) --sent.mlRestSendsLeft;

		AddBodyEntry(entries, candidate.mpProp, candidate.mlBodyIdx, candidate.mbRest);
		++lCount;

		if(lCount == kCoopMaxBodiesPerPacket)
		{
			SendBodyPacket(sMap, entries, lCount);
			entries = cLuxCoopBuffer();
			lCount = 0;
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

	//What the client holds is always sent, it is only ever a few bodies
	std::vector<cLuxCoopBodyCandidate> vCandidates;
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

		cLuxCoopBodyCandidate candidate;
		candidate.mpProp = pProp;
		candidate.mlBodyIdx = lBodyIdx;
		candidate.mbRest = false;
		candidate.mfPriority = 1.0f;
		vCandidates.push_back(candidate);
	}

	SendBodyCandidates(vCandidates);
}

//-----------------------------------------------------------------------

bool cLuxCoopNet::IsClientOwned(const tLuxCoopBodyKey& aKey)
{
	tLuxCoopOwnedBodyMap::iterator it = m_mapClientOwnedBodies.find(aKey);
	return it != m_mapClientOwnedBodies.end() && mfTime - it->second <= kCoopOwnershipLinger;
}

//-----------------------------------------------------------------------

//Entry layout, 24 bytes at rest and 36 moving:
//  name hash (4), body index (1), flags (1), position (3 floats),
//  rotation as the three smallest quaternion components (3 shorts, the index of the dropped one is in the flags),
//  then unless resting, linear and angular velocity (3 shorts each).
void cLuxCoopNet::AddBodyEntry(cLuxCoopBuffer &aBuffer, iLuxProp *apProp, int alBodyIdx, bool abRest)
{
	iPhysicsBody *pBody = apProp->GetBody(alBodyIdx);
	cMatrixf &mtxBody = pBody->GetLocalMatrix();
	cQuaternion qRot;
	qRot.FromRotationMatrix(mtxBody.GetRotation());
	qRot.Normalize();

	float vComp[4] = { qRot.w, qRot.v.x, qRot.v.y, qRot.v.z };
	int lLargest = 0;
	for(int i=1; i<4; ++i)
		if(cMath::Abs(vComp[i]) > cMath::Abs(vComp[lLargest])) lLargest = i;
	float fSign = vComp[lLargest] < 0 ? -1.0f : 1.0f;

	unsigned char lFlags = (unsigned char)lLargest;
	if(abRest) lFlags |= kCoopBodyFlag_Rest;

	aBuffer.AddInt((int)HashName(apProp->GetName()));
	aBuffer.AddByte((unsigned char)alBodyIdx);
	aBuffer.AddByte(lFlags);
	aBuffer.AddVector3f(mtxBody.GetTranslation());
	for(int i=0; i<4; ++i)
	{
		if(i == lLargest) continue;
		aBuffer.AddShort((short)cMath::RoundToInt(cMath::Clamp(vComp[i]*fSign / kCoopQuatCompMax, -1.0f, 1.0f) * 32767.0f));
	}

	if(abRest) return;

	cVector3f vLinVel = pBody->GetLinearVelocity();
	cVector3f vAngVel = pBody->GetAngularVelocity();
	for(int i=0; i<3; ++i) aBuffer.AddShort((short)cMath::RoundToInt(cMath::Clamp(vLinVel.v[i] / kCoopLinVelScale, -32767.0f, 32767.0f)));
	for(int i=0; i<3; ++i) aBuffer.AddShort((short)cMath::RoundToInt(cMath::Clamp(vAngVel.v[i] / kCoopAngVelScale, -32767.0f, 32767.0f)));
}

//-----------------------------------------------------------------------

void cLuxCoopNet::ApplyBodyState(iPhysicsBody *apBody, const cVector3f& avPos, const cQuaternion& aqRot, const cVector3f& avLinVel, const cVector3f& avAngVel, bool abRest)
{
	cMatrixf &mtxCurrent = apBody->GetLocalMatrix();
	cVector3f vCurrentPos = mtxCurrent.GetTranslation();
	cQuaternion qCurrent;
	qCurrent.FromRotationMatrix(mtxCurrent.GetRotation());
	qCurrent.Normalize();

	//Rotation that takes the local pose to the remote one
	cQuaternion qError = cMath::QuaternionMul(aqRot, cQuaternion(qCurrent.w, -qCurrent.v.x, -qCurrent.v.y, -qCurrent.v.z));
	if(qError.w < 0) qError = qError * -1.0f;
	float fAngleError = 2.0f * acosf(cMath::Min(qError.w, 1.0f));
	cVector3f vPosError = avPos - vCurrentPos;
	float fPosError = vPosError.Length();

	if(abRest)
	{
		//Resting bodies are put exactly in place, and left alone when they already are
		if(fPosError > kCoopBodyRestTolerance || fAngleError > kCoopBodyRestTolerance)
		{
			cMatrixf mtxTransform = cMath::MatrixQuaternion(aqRot);
			mtxTransform.SetTranslation(avPos);
			apBody->Enable();
			apBody->SetMatrix(mtxTransform);
		}
		apBody->SetLinearVelocity(0);
		apBody->SetAngularVelocity(0);
		return;
	}

	apBody->Enable();

	//Far off (teleported, just loaded, lost packets): snap
	if(fPosError > kCoopBodySnapDist || fAngleError > kCoopBodySnapAngle)
	{
		cMatrixf mtxTransform = cMath::MatrixQuaternion(aqRot);
		mtxTransform.SetTranslation(avPos);
		apBody->SetMatrix(mtxTransform);
		apBody->SetLinearVelocity(avLinVel);
		apBody->SetAngularVelocity(avAngVel);
		return;
	}

	//Close: steer with velocities so the local simulation glides onto the remote pose instead of jumping.
	//This also keeps doors and other jointed bodies from fighting their joints.
	cVector3f vAngCorrection(0);
	float fSinHalf = qError.v.Length();
	if(fSinHalf > 0.0001f) vAngCorrection = (qError.v / fSinHalf) * fAngleError * kCoopBodyCorrection;

	apBody->SetLinearVelocity(avLinVel + vPosError * kCoopBodyCorrection);
	apBody->SetAngularVelocity(avAngVel + vAngCorrection);
}

//-----------------------------------------------------------------------

unsigned int cLuxCoopNet::HashName(const tString& asName)
{
	//FNV-1a
	unsigned int lHash = 2166136261u;
	for(size_t i=0; i<asName.size(); ++i)
	{
		lHash ^= (unsigned char)asName[i];
		lHash *= 16777619u;
	}
	return lHash;
}

iLuxProp* cLuxCoopNet::GetPropByNameHash(unsigned int alHash)
{
	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	if(pMap==NULL) return NULL;

	tLuxCoopNameHashMap::iterator it = m_mapPropNameHashes.find(alHash);
	if(it != m_mapPropNameHashes.end())
	{
		iLuxProp *pProp = static_cast<iLuxProp*>(pMap->GetEntityByName(it->second, eLuxEntityType_Prop));
		if(pProp) return pProp;
	}

	//Unknown hash: a prop was created since the table was built (broken pieces, drops). Rebuild, but not every packet.
	if(mfTime - mfPropNameHashTime < kCoopNameHashRebuildTime) return NULL;
	mfPropNameHashTime = mfTime;

	m_mapPropNameHashes.clear();
	cLuxEntityIterator entityIt = pMap->GetEntityIterator();
	while(entityIt.HasNext())
	{
		iLuxEntity *pEntity = entityIt.Next();
		if(pEntity->GetEntityType() != eLuxEntityType_Prop) continue;
		m_mapPropNameHashes[HashName(pEntity->GetName())] = pEntity->GetName();
	}

	it = m_mapPropNameHashes.find(alHash);
	if(it == m_mapPropNameHashes.end()) return NULL;
	return static_cast<iLuxProp*>(pMap->GetEntityByName(it->second, eLuxEntityType_Prop));
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
	buffer.AddByte((unsigned char)alCount);
	buffer.AddBuffer(aEntries);
	Send(buffer, kCoopChannel_State, false);
}

//-----------------------------------------------------------------------

void cLuxCoopNet::Send(const cLuxCoopBuffer &aBuffer, int alChannel, bool abReliable)
{
	if(mpTransport==NULL || mpTransport->HasPeer()==false || aBuffer.GetSize()==0) return;
	mpTransport->Send(aBuffer.GetData(), aBuffer.GetSize(), alChannel, abReliable);
}

//-----------------------------------------------------------------------

tString cLuxCoopNet::GetCurrentMapFile()
{
	cLuxMap *pMap = gpBase->mpMapHandler ? gpBase->mpMapHandler->GetCurrentMap() : NULL;
	if(pMap==NULL) return "";
	return cString::ToLowerCase(cString::GetFileName(pMap->GetFileName()));
}

//-----------------------------------------------------------------------
