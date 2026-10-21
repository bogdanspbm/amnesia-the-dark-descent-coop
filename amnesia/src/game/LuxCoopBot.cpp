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

#include "LuxCoopBot.h"

#include "LuxMapHandler.h"
#include "LuxMap.h"
#include "LuxPlayer.h"
#include "LuxPlayerHelpers.h"
#include "LuxAreaNodes.h"
#include "LuxEnemy.h"
#include "LuxMapHelper.h"
#include "LuxProp_Item.h"

//////////////////////////////////////////////////////////////////////////
// CONSTANTS
//////////////////////////////////////////////////////////////////////////

static const float kBotStartDelay = 1.5f;			//Let the map settle (and the partner arrive) before acting
static const float kBotTurnSpeed = 4.0f;			//How fast the view closes the gap to where it wants to look (1/s)
static const float kBotMaxTurnRate = cMath::ToRad(160);
static const float kBotArriveDist = 0.6f;
static const float kBotStuckCheckTime = 0.8f;			//Progress is measured over this window
static const float kBotStuckMinMove = 0.25f;			//Less than this in a window counts as stuck
static const float kBotDefaultGoToTime = 30.0f;			//Steps without a Time still give up eventually
static const float kBotDefaultFleeTime = 20.0f;
static const float kBotRepathTime = 1.0f;
static const float kBotWalkPitch = cMath::ToRad(-6);	//Eyes a little down while walking, like a person watching the floor

//Path node containers of the map's own monsters; their cached .nodes files ship with the game
static const char* gsBotNodeContainers[] = { "servant_grunt", "servant_brute" };

//////////////////////////////////////////////////////////////////////////
// HELPERS
//////////////////////////////////////////////////////////////////////////

static float WrapAngle(float afX)
{
	while(afX > kPif) afX -= k2Pif;
	while(afX < -kPif) afX += k2Pif;
	return afX;
}

static float Rand(float afMin, float afMax)
{
	return cMath::RandRectf(afMin, afMax);
}

//////////////////////////////////////////////////////////////////////////
// CONSTRUCTORS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cLuxCoopBot::cLuxCoopBot()
{
	mlStep = 0;
	mbHost = true;
	mbStarted = false;
	mbDone = false;
	mbInvulnerable = true;
	mbKeepSanity = true;
	mfStartDelay = kBotStartDelay;
	mfSceneTime = 0;
	mfLength = 0;
	mfStepTime = 0;
	mfStepCount = 0;
	mbPausing = false;
	mbPartnerReady = false;
	mpNodeContainer = NULL;
	mpAStar = NULL;
	mpPathWorld = NULL;
	mfStuckTime = 0;
	mlStuckCount = 0;
	mlSidestep = 0;
	mbRunning = false;
	mbCrouching = false;
	mfLookTargetTime = 0;
	mvMoveDir = 0;
	mvLookAheadDir = 0;
	mfLookBackCount = 0;
	mfGlanceCount = 3;
	mfNoiseT = 0;
}

cLuxCoopBot::~cLuxCoopBot()
{
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

bool cLuxCoopBot::LoadScene(const tString& asFile, bool abHost)
{
	mvSteps.clear();
	mbHost = abHost;

	//Scenes are plain files next to the exe, not game resources
	iXmlDocument *pDoc = gpBase->mpEngine->GetResources()->GetLowLevel()->CreateXmlDocument();
	if(pDoc->CreateFromFile(cString::To16Char(asFile))==false)
	{
		Error("Coop bot: could not load scene '%s'\n", asFile.c_str());
		hplDelete(pDoc);
		return false;
	}

	mbInvulnerable = pDoc->GetAttributeBool("Invulnerable", true);
	mbKeepSanity = pDoc->GetAttributeBool("KeepSanity", true);
	mfStartDelay = pDoc->GetAttributeFloat("StartDelay", kBotStartDelay);
	mfLength = pDoc->GetAttributeFloat("Length", 0);

	cXmlElement *pPart = pDoc->GetFirstElement(abHost ? "Host" : "Client");
	if(pPart)
	{
		cXmlNodeListIterator it = pPart->GetChildIterator();
		while(it.HasNext())
		{
			cXmlElement *pElem = it.Next()->ToElement();
			if(pElem==NULL) continue;

			const tString& sType = pElem->GetValue();
			cLuxCoopBotStep step;
			step.msTarget = pElem->GetAttributeString("Target", "");
			step.msCode = pElem->GetAttributeString("Code", "");
			step.mfTime = pElem->GetAttributeFloat("Time", 0);
			step.mfDist = pElem->GetAttributeFloat("Dist", 0);
			step.mbOn = pElem->GetAttributeBool("On", true);
			step.mbRun = pElem->GetAttributeBool("Run", false);
			step.msFrom = pElem->GetAttributeString("From", "");
			step.msEnemyActive = pElem->GetAttributeString("EnemyActive", "");
			step.mfPartnerWithin = pElem->GetAttributeFloat("PartnerWithin", 0);
			step.mbLookBack = pElem->GetAttributeBool("LookBack", true);
			if(cString::ToLowerCase(step.msTarget)=="partner") step.msTarget = "Partner";
			if(cString::ToLowerCase(step.msTarget)=="enemy") step.msTarget = "Enemy";

			if(sType == "GoTo")				step.mType = eLuxCoopBotAction_GoTo;
			else if(sType == "Look")		step.mType = eLuxCoopBotAction_Look;
			else if(sType == "Follow")		step.mType = eLuxCoopBotAction_Follow;
			else if(sType == "Wander")		{ step.mType = eLuxCoopBotAction_Wander; step.mfDist = pElem->GetAttributeFloat("Radius", 8); }
			else if(sType == "Flee")		step.mType = eLuxCoopBotAction_Flee;
			else if(sType == "Wait")		step.mType = eLuxCoopBotAction_Wait;
			else if(sType == "WaitUntil")	step.mType = eLuxCoopBotAction_WaitUntil;
			else if(sType == "Lantern")		step.mType = eLuxCoopBotAction_Lantern;
			else if(sType == "Run")			step.mType = eLuxCoopBotAction_Run;
			else if(sType == "Crouch")		step.mType = eLuxCoopBotAction_Crouch;
			else if(sType == "Script")		step.mType = eLuxCoopBotAction_Script;
			else if(sType == "Interact")	step.mType = eLuxCoopBotAction_Interact;
			else if(sType == "End")			step.mType = eLuxCoopBotAction_End;
			else
			{
				Warning("Coop bot: unknown step '%s' in '%s'\n", sType.c_str(), asFile.c_str());
				continue;
			}
			mvSteps.push_back(step);
		}
	}
	hplDelete(pDoc);

	if(mvSteps.empty())
	{
		Warning("Coop bot: scene '%s' has no steps for the %s\n", asFile.c_str(), abHost ? "host" : "client");
		return false;
	}

	Log("Coop bot: scene '%s', %d steps as %s\n", asFile.c_str(), (int)mvSteps.size(), abHost ? "host" : "client");
	return true;
}

//-----------------------------------------------------------------------

void cLuxCoopBot::OnMapEnter()
{
	//A path container belongs to the world it was made in
	mpNodeContainer = NULL;
	mpAStar = NULL;
	mpPathWorld = NULL;
	mlstPath.clear();
}

//-----------------------------------------------------------------------

void cLuxCoopBot::Update(float afTimeStep, bool abPartnerReady, const cVector3f& avPartnerPos)
{
	if(mvSteps.empty() || mbDone) return;

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	cLuxPlayer *pPlayer = gpBase->mpPlayer;
	if(pMap==NULL || pPlayer->IsActive()==false || pPlayer->IsDead()) return;

	mbPartnerReady = abPartnerReady;
	mvPartnerPos = avPartnerPos;

	if(mbStarted==false)
	{
		mfStartDelay -= afTimeStep;
		if(mfStartDelay > 0) return;

		mbStarted = true;
		mfSceneTime = 0;
		mlStep = 0;
		mvLastPos = GetFeetPos();
		Log("Coop bot: scene started\n");
		StartStep();
	}

	if(mpPathWorld != pMap->GetWorld()) SetupPathfinding();

	mfSceneTime += afTimeStep;
	if(mfLength > 0 && mfSceneTime >= mfLength)
	{
		mlStep = mvSteps.size()-1;
		NextStep();
		return;
	}

	//Filming: nobody should die or collapse halfway through a take
	if(mbInvulnerable && pPlayer->GetHealth() < 100) pPlayer->SetHealth(100);
	if(mbKeepSanity && pPlayer->GetSanity() < 80) pPlayer->SetSanity(80);

	while(mbDone==false && UpdateStep(afTimeStep))
		NextStep();

	UpdateLook(afTimeStep);
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

void cLuxCoopBot::NextStep()
{
	++mlStep;
	if(mlStep >= mvSteps.size())
	{
		StopMoving();
		mbDone = true;
		Log("Coop bot: scene finished\n");
		return;
	}
	StartStep();
}

void cLuxCoopBot::StartStep()
{
	cLuxCoopBotStep& step = mvSteps[mlStep];
	cLuxPlayer *pPlayer = gpBase->mpPlayer;
	mfStepTime = 0;
	mfStepCount = 0;
	mbPausing = false;
	mlstPath.clear();

	switch(step.mType)
	{
	case eLuxCoopBotAction_GoTo:
		if(GetTargetPos(step.msTarget, mvStepGoal)==false)
		{
			Warning("Coop bot: GoTo target '%s' not found\n", step.msTarget.c_str());
			mvStepGoal = GetFeetPos();
		}
		if(step.mbRun) { mbRunning = true; pPlayer->Run(true); }
		PathTo(mvStepGoal);
		break;
	case eLuxCoopBotAction_Lantern:
		pPlayer->GetHelperLantern()->SetActive(step.mbOn, true, false, false);
		break;
	case eLuxCoopBotAction_Run:
		mbRunning = step.mbOn;
		pPlayer->Run(step.mbOn);
		break;
	case eLuxCoopBotAction_Crouch:
		//Crouch is a toggle in the game, so press only when the state has to change
		if(step.mbOn != mbCrouching)
		{
			pPlayer->Crouch(true);
			pPlayer->Crouch(false);
			mbCrouching = step.mbOn;
		}
		break;
	case eLuxCoopBotAction_Script:
		Log("Coop bot: script %s\n", step.msCode.c_str());
		gpBase->mpMapHandler->GetCurrentMap()->RunScript(step.msCode);
		break;
	case eLuxCoopBotAction_Flee:
		mbRunning = true;
		pPlayer->Run(true);
		break;
	default:
		break;
	}
}

//-----------------------------------------------------------------------

bool cLuxCoopBot::UpdateStep(float afTimeStep)
{
	cLuxCoopBotStep& step = mvSteps[mlStep];
	mfStepTime += afTimeStep;
	mfStepCount -= afTimeStep;

	switch(step.mType)
	{
	///////////////////////////
	case eLuxCoopBotAction_GoTo:
	{
		//"Partner" and "Enemy" move, so aim again now and then
		if(mfStepCount <= 0 && (step.msTarget == "Partner" || step.msTarget == "Enemy"))
		{
			mfStepCount = kBotRepathTime;
			if(GetTargetPos(step.msTarget, mvStepGoal)) PathTo(mvStepGoal);
		}
		bool bArrived = MoveTowards(mvStepGoal, step.mfDist > 0 ? step.mfDist : kBotArriveDist, afTimeStep);
		if(bArrived || mfStepTime > (step.mfTime > 0 ? step.mfTime : kBotDefaultGoToTime))
		{
			StopMoving();
			if(step.mbRun) { mbRunning = false; gpBase->mpPlayer->Run(false); }
			return true;
		}
		return false;
	}

	///////////////////////////
	case eLuxCoopBotAction_Look:
	{
		cVector3f vPos;
		if(GetTargetPos(step.msTarget, vPos)) SetLookTarget(vPos + cVector3f(0, step.msTarget=="Partner" ? 1.5f : 0, 0), 0.2f);
		return mfStepTime >= cMath::Max(step.mfTime, 0.3f);
	}

	///////////////////////////
	case eLuxCoopBotAction_Follow:
	{
		float fDist = step.mfDist > 0 ? step.mfDist : 2.5f;
		cVector3f vTarget;
		bool bHasTarget = GetTargetPos(step.msTarget=="" ? "Partner" : step.msTarget, vTarget);
		if(bHasTarget)
		{
			float fToTarget = cMath::Vector3Dist(GetFeetPos(), vTarget);
			if(fToTarget > fDist)
			{
				if(mfStepCount <= 0 || mlstPath.empty())
				{
					mfStepCount = kBotRepathTime;
					PathTo(vTarget);
				}
				//Catch up at a run when left far behind
				bool bShouldRun = fToTarget > fDist + 5;
				if(bShouldRun != mbRunning) { mbRunning = bShouldRun; gpBase->mpPlayer->Run(bShouldRun); }
				MoveTowards(vTarget, fDist, afTimeStep);
			}
			else
			{
				StopMoving();
				if(mbRunning) { mbRunning = false; gpBase->mpPlayer->Run(false); }
				//Waiting for the partner: look at them now and then, otherwise look around
				if(mfLookTargetTime <= 0 && Rand(0,1) < afTimeStep*0.4f) SetLookTarget(vTarget + cVector3f(0,1.5f,0), Rand(1.0f, 2.5f));
			}
		}
		return step.mfTime > 0 && mfStepTime >= step.mfTime;
	}

	///////////////////////////
	case eLuxCoopBotAction_Wander:
	{
		if(mbPausing)
		{
			if(mfStepCount <= 0) mbPausing = false;
		}
		else if(mlstPath.empty() || MoveTowards(mvStepGoal, kBotArriveDist, afTimeStep))
		{
			//Arrived somewhere: stop, look around for a moment, pick the next spot
			StopMoving();
			if(PickWanderGoal(step.mfDist, mvStepGoal))
			{
				PathTo(mvStepGoal);
				if(mfStepTime > 0.5f)
				{
					mbPausing = true;
					mfStepCount = Rand(1.0f, 3.0f);
					mfGlanceCount = 0;
				}
			}
		}
		return step.mfTime > 0 && mfStepTime >= step.mfTime;
	}

	///////////////////////////
	case eLuxCoopBotAction_Flee:
	{
		cVector3f vEnemy;
		bool bEnemy = step.msFrom != "" ? GetTargetPos(step.msFrom, vEnemy) : GetNearestEnemy(vEnemy);
		bool bArrived = false;
		if(step.msTarget != "")
		{
			//Running to a set place, eg the next room's door
			if(mfStepCount <= 0 || mlstPath.empty())
			{
				mfStepCount = 100000.0f;
				if(GetTargetPos(step.msTarget, mvStepGoal)) PathTo(mvStepGoal);
			}
			bArrived = MoveTowards(mvStepGoal, step.mfDist > 0 ? step.mfDist : kBotArriveDist, afTimeStep);
		}
		else
		{
			if(bEnemy && (mfStepCount <= 0 || mlstPath.empty()))
			{
				mfStepCount = 2.0f;
				if(PickFleeGoal(vEnemy, mvStepGoal)) PathTo(mvStepGoal);
			}
			if(mlstPath.empty()==false) MoveTowards(mvStepGoal, kBotArriveDist, afTimeStep);
		}

		//Glance back over the shoulder, which is what makes a chase look like one
		//(often and long enough while it is close, so it actually ends up in the shot)
		if(step.mbLookBack && bEnemy && mfLookTargetTime <= 0)
		{
			mfLookBackCount -= afTimeStep;
			if(mfLookBackCount <= 0)
			{
				bool bClose = cMath::Vector3Dist(vEnemy, GetFeetPos()) < 12;
				mfLookBackCount = bClose ? Rand(2.0f, 3.0f) : Rand(4.0f, 6.0f);
				SetLookTarget(vEnemy + cVector3f(0,1.4f,0), bClose ? Rand(0.9f, 1.4f) : Rand(0.6f, 0.9f));
			}
		}

		bool bOver = bArrived || mfStepTime >= (step.mfTime > 0 ? step.mfTime : kBotDefaultFleeTime);
		if(bOver)
		{
			StopMoving();
			mbRunning = false;
			gpBase->mpPlayer->Run(false);
		}
		return bOver;
	}

	///////////////////////////
	case eLuxCoopBotAction_Wait:
		return mfStepTime >= step.mfTime;
	case eLuxCoopBotAction_WaitUntil:
	{
		if(step.mfTime > 0 && mfSceneTime < step.mfTime) return false;
		if(step.mfPartnerWithin > 0 && (mbPartnerReady==false || cMath::Vector3Dist(mvPartnerPos, GetFeetPos()) > step.mfPartnerWithin)) return false;
		if(step.msEnemyActive != "")
		{
			iLuxEntity *pEnemy = gpBase->mpMapHandler->GetCurrentMap()->GetEntityByName(step.msEnemyActive);
			if(pEnemy==NULL || pEnemy->IsActive()==false) return false;
		}
		return true;
	}

	///////////////////////////
	case eLuxCoopBotAction_Interact:
	{
		//Notes and diaries open the journal, which would stop the scene
		iLuxEntity *pEntity = gpBase->mpMapHandler->GetCurrentMap()->GetEntityByName(step.msTarget);
		if(mfStepTime <= afTimeStep && pEntity && pEntity->GetEntityType()==eLuxEntityType_Prop &&
			static_cast<iLuxProp*>(pEntity)->GetPropType()==eLuxPropType_Item)
		{
			eLuxItemType type = static_cast<cLuxProp_Item*>(pEntity)->GetItemType();
			if(type == eLuxItemType_Note || type == eLuxItemType_Diary)
			{
				Log("Coop bot: not opening '%s', it would open the journal\n", step.msTarget.c_str());
				mbPausing = true;
			}
		}

		//Aim at it for a moment, press, hold a little, let go
		cVector3f vPos;
		if(GetTargetPos(step.msTarget, vPos)) SetLookTarget(vPos, 0.2f);
		if(mfStepTime >= 0.8f && mbPausing==false)
		{
			mbPausing = true;
			gpBase->mpPlayer->DoAction(eLuxPlayerAction_Interact, true);
		}
		if(mfStepTime >= 1.1f)
		{
			gpBase->mpPlayer->DoAction(eLuxPlayerAction_Interact, false);
			return true;
		}
		return false;
	}
	case eLuxCoopBotAction_End:
		mlStep = mvSteps.size()-1;
		return true;
	default:
		return true;
	}
}

//-----------------------------------------------------------------------

bool cLuxCoopBot::MoveTowards(const cVector3f& avGoal, float afArriveDist, float afTimeStep)
{
	cLuxPlayer *pPlayer = gpBase->mpPlayer;
	cVector3f vPos = GetFeetPos();

	cVector3f vToGoal = avGoal - vPos;	vToGoal.y = 0;
	if(vToGoal.Length() <= afArriveDist)
	{
		mlstPath.clear();
		return true;
	}

	//Drop path points we have reached; the final goal is the last one
	while(mlstPath.empty()==false)
	{
		cVector3f vToNode = mlstPath.front() - vPos;	vToNode.y = 0;
		if(vToNode.Length() > (mlstPath.size()==1 ? afArriveDist : kBotArriveDist)) break;
		mlstPath.pop_front();
	}
	cVector3f vNext = mlstPath.empty() ? avGoal : mlstPath.front();

	cVector3f vDir = vNext - vPos;	vDir.y = 0;
	float fLen = vDir.Length();
	if(fLen < 0.001f) return false;
	vDir = vDir / fLen;
	mvMoveDir = vDir;

	//Look where the path goes a few meters ahead, not at the next node, which on a corner is a wall
	cVector3f vAhead = vPos;
	float fLeft = 3.5f;
	cVector3f vFrom = vPos;
	for(std::list<cVector3f>::iterator it = mlstPath.begin(); it != mlstPath.end() && fLeft > 0; ++it)
	{
		cVector3f vSeg = *it - vFrom;	vSeg.y = 0;
		float fSeg = vSeg.Length();
		if(fSeg >= fLeft) { vAhead = vFrom + vSeg * (fLeft / fSeg); fLeft = 0; break; }
		fLeft -= fSeg;
		vFrom = *it;
		vAhead = *it;
	}
	if(mlstPath.empty()) vAhead = avGoal;
	cVector3f vLookAhead = vAhead - vPos;	vLookAhead.y = 0;
	mvLookAheadDir = vLookAhead.Length() > 0.3f ? vLookAhead / vLookAhead.Length() : vDir;

	//Walk along the path while the head may look elsewhere: split the direction into the view's forward and right
	cCamera *pCam = pPlayer->GetCamera();
	cVector3f vFwd = pCam->GetForward();	vFwd.y = 0;	vFwd.Normalize();
	cVector3f vRight = pCam->GetRight();	vRight.y = 0;	vRight.Normalize();
	float fFwd = cMath::Vector3Dot(vDir, vFwd);
	float fRight = cMath::Vector3Dot(vDir, vRight);
	pPlayer->Move(eCharDir_Forward, fFwd);
	pPlayer->Move(eCharDir_Right, fRight);

	//Stuck on a corner or a prop: measured as too little progress over a short window.
	//First sidestep (one side for the whole try), then skip the point, then give up.
	if(mlSidestep != 0) pPlayer->Move(eCharDir_Right, (float)mlSidestep);

	mfStuckTime += afTimeStep;
	if(mfStuckTime < kBotStuckCheckTime) return false;

	float fMoved = cMath::Vector3Dist(vPos, mvLastPos);
	mvLastPos = vPos;
	mfStuckTime = 0;
	if(fMoved >= kBotStuckMinMove)
	{
		mlStuckCount = 0;
		mlSidestep = 0;
		return false;
	}

	++mlStuckCount;
	if(mlStuckCount == 1)
	{
		mlSidestep = Rand(0,1) < 0.5f ? 1 : -1;
	}
	else if(mlStuckCount == 2)
	{
		mlSidestep = -mlSidestep;
	}
	else
	{
		mlSidestep = 0;
		mlStuckCount = 0;
		if(mlstPath.size() > 1)	mlstPath.pop_front();
		else
		{
			Log("Coop bot: stuck, giving up on (%s)\n", avGoal.ToString().c_str());
			mlstPath.clear();
			mfGlanceCount = 0;
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------

bool cLuxCoopBot::PathTo(const cVector3f& avGoal)
{
	mlstPath.clear();
	mfStuckTime = 0;
	mlStuckCount = 0;
	mlSidestep = 0;
	mvLastPos = GetFeetPos();

	if(mpAStar)
	{
		tAINodeList lstNodes;
		cVector3f vStart = GetFeetPos() + cVector3f(0, 0.05f, 0);
		if(mpAStar->GetPath(vStart, avGoal, &lstNodes))
		{
			//The list runs from the goal back to the start
			for(tAINodeList::reverse_iterator it = lstNodes.rbegin(); it != lstNodes.rend(); ++it)
				mlstPath.push_back((*it)->GetPosition());
		}
	}
	mlstPath.push_back(avGoal);
	return true;
}

void cLuxCoopBot::StopMoving()
{
	//Look somewhere open right away instead of at whatever we walked up to
	if(mvMoveDir.Length() > 0.5f) mfGlanceCount = 0;
	mlstPath.clear();
	mvMoveDir = 0;
	mvLookAheadDir = 0;
	mfStuckTime = 0;
	mlStuckCount = 0;
	mlSidestep = 0;
}

//-----------------------------------------------------------------------

void cLuxCoopBot::SetLookTarget(const cVector3f& avPos, float afTime)
{
	mvLookTarget = avPos;
	mfLookTargetTime = afTime;
}

void cLuxCoopBot::UpdateLook(float afTimeStep)
{
	cLuxPlayer *pPlayer = gpBase->mpPlayer;
	cCamera *pCam = pPlayer->GetCamera();
	mfNoiseT += afTimeStep;

	/////////////////////////
	// Where to look: a set target, else where we walk, else the occasional glance around
	cVector3f vWantDir;
	bool bHaveDir = false;
	if(mfLookTargetTime > 0)
	{
		mfLookTargetTime -= afTimeStep;
		vWantDir = mvLookTarget - pCam->GetPosition();
		bHaveDir = vWantDir.Length() > 0.01f;
	}
	if(bHaveDir==false && mvMoveDir.Length() > 0.5f)
	{
		vWantDir = mvLookAheadDir.Length() > 0.5f ? mvLookAheadDir : mvMoveDir;
		vWantDir.y = sinf(kBotWalkPitch);
		bHaveDir = true;

		//Walking with someone: look at them from time to time
		mfGlanceCount -= afTimeStep;
		if(mfGlanceCount <= 0)
		{
			mfGlanceCount = Rand(3.0f, 7.0f);
			if(mbPartnerReady && cMath::Vector3Dist(mvPartnerPos, GetFeetPos()) < 12 && Rand(0,1) < 0.5f)
				SetLookTarget(mvPartnerPos + cVector3f(0,1.5f,0), Rand(0.6f, 1.2f));
		}
	}
	if(bHaveDir==false)
	{
		//Standing still: look around the room, never at a wall
		mfGlanceCount -= afTimeStep;
		if(mfGlanceCount <= 0)
		{
			mfGlanceCount = Rand(1.5f, 3.5f);
			cVector3f vLook;
			if(PickIdleLook(vLook)) SetLookTarget(vLook, mfGlanceCount);
		}
		if(mfLookTargetTime <= 0) return;
		vWantDir = mvLookTarget - pCam->GetPosition();
	}

	/////////////////////////
	// Turn smoothly, with a little hand-held wobble
	cVector3f vFwd = pCam->GetForward();
	cVector3f vFwdFlat(vFwd.x, 0, vFwd.z);
	cVector3f vWantFlat(vWantDir.x, 0, vWantDir.z);
	float fYawErr = 0;
	if(vFwdFlat.Length() > 0.001f && vWantFlat.Length() > 0.001f)
	{
		vFwdFlat.Normalize();
		vWantFlat.Normalize();
		//Signed angle around +Y; a positive yaw turns left
		fYawErr = atan2f(vFwdFlat.z*vWantFlat.x - vFwdFlat.x*vWantFlat.z, cMath::Vector3Dot(vFwdFlat, vWantFlat));
	}
	float fWantPitch = atan2f(vWantDir.y, cMath::Max(0.001f, sqrtf(vWantDir.x*vWantDir.x + vWantDir.z*vWantDir.z)));
	fWantPitch += sinf(mfNoiseT*0.7f) * cMath::ToRad(1.5f);
	float fPitchErr = fWantPitch - pCam->GetPitch();
	fYawErr = WrapAngle(fYawErr + sinf(mfNoiseT*0.53f) * cMath::ToRad(2.0f));

	float fMaxStep = kBotMaxTurnRate * afTimeStep;
	float fYawStep = cMath::Clamp(fYawErr * kBotTurnSpeed * afTimeStep, -fMaxStep, fMaxStep);
	float fPitchStep = cMath::Clamp(fPitchErr * kBotTurnSpeed * afTimeStep, -fMaxStep, fMaxStep);

	//The player doubles these (see cLuxPlayer::AddYaw)
	pPlayer->AddYaw(fYawStep * 0.5f);
	pPlayer->AddPitch(fPitchStep * 0.5f);
}

//-----------------------------------------------------------------------

bool cLuxCoopBot::PickIdleLook(cVector3f& avLook)
{
	cCamera *pCam = gpBase->mpPlayer->GetCamera();
	cVector3f vEye = pCam->GetPosition();
	cLuxMapHelper *pHelper = gpBase->mpMapHelper;

	//The partner, if close and in sight
	cVector3f vPartnerHead = mvPartnerPos + cVector3f(0,1.5f,0);
	if(mbPartnerReady && cMath::Vector3Dist(vPartnerHead, vEye) < 15 && Rand(0,1) < 0.4f &&
		pHelper->CheckLineOfSight(vEye, vPartnerHead, false))
	{
		avLook = vPartnerHead;
		return true;
	}

	//Open directions: try a spread of yaws around the current one, nearest first, and keep the first with 3 m of free view
	float fYaw = pCam->GetYaw();
	float fStart = Rand(0,1) < 0.5f ? 1.0f : -1.0f;
	for(int i=1; i<=8; ++i)
	{
		float fTry = fYaw + fStart * (float)((i+1)/2) * cMath::ToRad(40) * ((i%2)==1 ? 1.0f : -1.0f) + Rand(-0.2f, 0.2f);
		cVector3f vDir(-sinf(fTry), Rand(-0.2f, 0.1f), -cosf(fTry));
		if(pHelper->CheckLineOfSight(vEye, vEye + vDir*3.0f, false))
		{
			avLook = vEye + vDir*5.0f;
			return true;
		}
	}

	//Boxed in: turn around
	avLook = vEye + cVector3f(sinf(fYaw), 0, cosf(fYaw)) * 5.0f;
	return true;
}

//-----------------------------------------------------------------------

bool cLuxCoopBot::GetTargetPos(const tString& asTargetName, cVector3f& avPos)
{
	if(asTargetName == "") return false;
	tString sLower = cString::ToLowerCase(asTargetName);
	tString asTarget = sLower=="partner" ? "Partner" : sLower=="enemy" ? "Enemy" : asTargetName;
	if(asTarget == "Partner")
	{
		if(mbPartnerReady==false) return false;
		avPos = mvPartnerPos;
		return true;
	}
	if(asTarget == "Enemy") return GetNearestEnemy(avPos);

	//"x y z"
	tStringVec vNums;
	tString sSep = " ,";
	cString::GetStringVec(asTarget, vNums, &sSep);
	if(vNums.size()==3 && (isdigit(asTarget[0]) || asTarget[0]=='-'))
	{
		avPos = cVector3f(cString::ToFloat(vNums[0].c_str(),0), cString::ToFloat(vNums[1].c_str(),0), cString::ToFloat(vNums[2].c_str(),0));
		return true;
	}

	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	iLuxEntity *pEntity = pMap->GetEntityByName(asTarget);
	if(pEntity && pEntity->GetAttachEntity())
	{
		avPos = pEntity->GetAttachEntity()->GetWorldPosition();
		return true;
	}

	//Looked up by hand, GetPlayerStart warns on every miss
	for(int i=0; i<pMap->GetPlayerStartNodeNum(); ++i)
	{
		cLuxNode_PlayerStart *pStart = pMap->GetPlayerStartNode(i);
		if(cString::ToLowerCase(pStart->GetName()) == sLower)
		{
			avPos = pStart->GetPosition();
			return true;
		}
	}

	if(mpNodeContainer)
	{
		cAINode *pNode = mpNodeContainer->GetNodeFromName(asTarget);
		if(pNode)
		{
			avPos = pNode->GetPosition();
			return true;
		}
	}
	if(m_setMissingTargets.insert(sLower).second)
		Warning("Coop bot: target '%s' not found on this map\n", asTargetName.c_str());
	return false;
}

bool cLuxCoopBot::GetNearestEnemy(cVector3f& avPos)
{
	cLuxMap *pMap = gpBase->mpMapHandler->GetCurrentMap();
	cVector3f vPos = GetFeetPos();
	float fBest = 1e10f;
	bool bFound = false;

	cLuxEntityIterator it = pMap->GetEntityIterator();
	while(it.HasNext())
	{
		iLuxEntity *pEntity = it.Next();
		if(pEntity->GetEntityType() != eLuxEntityType_Enemy || pEntity->IsActive()==false) continue;

		iLuxEnemy *pEnemy = static_cast<iLuxEnemy*>(pEntity);
		cVector3f vEnemy = pEnemy->GetCharacterBody()->GetFeetPosition();
		float fDist = cMath::Vector3DistSqr(vPos, vEnemy);
		if(fDist < fBest)
		{
			fBest = fDist;
			avPos = vEnemy;
			bFound = true;
		}
	}
	return bFound;
}

//-----------------------------------------------------------------------

bool cLuxCoopBot::PickWanderGoal(float afRadius, cVector3f& avGoal)
{
	if(mpNodeContainer==NULL || mpNodeContainer->GetNodeNum()==0)
	{
		//No nodes: a short random stroll in the open
		float fAngle = Rand(0, k2Pif);
		avGoal = GetFeetPos() + cVector3f(cosf(fAngle), 0, sinf(fAngle)) * Rand(1.5f, cMath::Max(2.0f, afRadius*0.5f));
		return true;
	}

	cVector3f vPos = GetFeetPos();
	std::vector<cAINode*> vCandidates;
	cAINodeIterator it = mpNodeContainer->GetNodeIterator(vPos, afRadius);
	while(it.HasNext())
	{
		cAINode *pNode = it.Next();
		float fDist = cMath::Vector3Dist(pNode->GetPosition(), vPos);
		if(fDist > 2.0f && fDist < afRadius) vCandidates.push_back(pNode);
	}
	if(vCandidates.empty()) return false;

	avGoal = vCandidates[cMath::RandRectl(0, (int)vCandidates.size()-1)]->GetPosition();
	return true;
}

bool cLuxCoopBot::PickFleeGoal(const cVector3f& avEnemyPos, cVector3f& avGoal)
{
	cVector3f vPos = GetFeetPos();
	cVector3f vAway = vPos - avEnemyPos;	vAway.y = 0;
	if(vAway.Length() < 0.01f) vAway = cVector3f(1,0,0);
	vAway.Normalize();

	if(mpNodeContainer==NULL || mpNodeContainer->GetNodeNum()==0)
	{
		avGoal = vPos + vAway * 6;
		return true;
	}

	//The node that gains the most distance from the monster, preferring ones ahead of us
	float fBestScore = -1e10f;
	cAINode *pBest = NULL;
	cAINodeIterator it = mpNodeContainer->GetNodeIterator(vPos, 18);
	while(it.HasNext())
	{
		cAINode *pNode = it.Next();
		cVector3f vToNode = pNode->GetPosition() - vPos;	vToNode.y = 0;
		float fDist = vToNode.Length();
		if(fDist < 4 || fDist > 18) continue;

		float fScore = cMath::Vector3Dist(pNode->GetPosition(), avEnemyPos) + cMath::Vector3Dot(vToNode / fDist, vAway) * 4.0f;
		if(fScore > fBestScore)
		{
			fBestScore = fScore;
			pBest = pNode;
		}
	}
	if(pBest==NULL) return false;

	avGoal = pBest->GetPosition();
	return true;
}

//-----------------------------------------------------------------------

void cLuxCoopBot::SetupPathfinding()
{
	cWorld *pWorld = gpBase->mpMapHandler->GetCurrentMap()->GetWorld();
	mpPathWorld = pWorld;
	mpNodeContainer = NULL;
	mpAStar = NULL;

	//Only use a container whose node cache already exists, so nothing is written into the game's map folders
	tWString sMapBase = cString::SetFileExtW(pWorld->GetFilePath(), _W(""));
	for(size_t i=0; i<sizeof(gsBotNodeContainers)/sizeof(gsBotNodeContainers[0]); ++i)
	{
		tWString sNodesFile = cString::SetFileExtW(sMapBase + _W("_") + cString::To16Char(gsBotNodeContainers[i]), _W("nodes"));
		if(cPlatform::FileExists(sNodesFile)==false) continue;

		mpNodeContainer = pWorld->CreateAINodeContainer(gsBotNodeContainers[i], "Default", gpBase->mpPlayer->GetCharacterBody()->GetSize(), false, 2, 6, 5.0f, 0.41f);
		if(mpNodeContainer)
		{
			mpAStar = pWorld->CreateAStarHandler(mpNodeContainer);
			Log("Coop bot: path nodes from '%s' (%d nodes)\n", gsBotNodeContainers[i], mpNodeContainer->GetNodeNum());
			return;
		}
	}
	Log("Coop bot: no path nodes on this map, walking in straight lines\n");
}

//-----------------------------------------------------------------------

cVector3f cLuxCoopBot::GetFeetPos()
{
	return gpBase->mpPlayer->GetCharacterBody()->GetFeetPosition();
}

//-----------------------------------------------------------------------
