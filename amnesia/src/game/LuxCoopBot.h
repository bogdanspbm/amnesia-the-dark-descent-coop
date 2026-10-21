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

#ifndef LUX_COOP_BOT_H
#define LUX_COOP_BOT_H

//----------------------------------------------

#include "LuxBase.h"

//----------------------------------------------

/**
 * Scene bot: drives the local player from a scene file, for filming co-op footage
 * without anyone at the keyboard. Each game plays its own part of the scene:
 *
 * <Scene Invulnerable="true" KeepSanity="true" Length="75">      Length: the scene ends after this many seconds
 *   <Host>
 *     <Lantern On="true"/>
 *     <GoTo Target="PathNodeArea_12"/>          entity, area, path node, player start, "Partner", "Enemy" or "x y z"
 *     <Look Target="Partner" Time="2"/>
 *     <Follow Target="Partner" Dist="2.5" Time="20"/>
 *     <Wander Radius="8" Time="15"/>
 *     <Flee Time="12" From="grunt_normal_2" To="level_celler_1" LookBack="true"/>
 *     <Interact Target="potion_oil_1"/>       look at it and press Interact (pick up, open, read)
 *     <Run On="true"/>  <Crouch On="false"/>
 *     <Wait Time="1.5"/>
 *     <WaitUntil Time="30" PartnerWithin="3" EnemyActive="grunt_normal_2"/>   all given conditions; Time is since the scene started
 *     <Script Code='SetEntityActive("servant_grunt_1", true);'/>
 *     <End/>
 *   </Host>
 *   <Client> ... </Client>
 * </Scene>
 */

enum eLuxCoopBotAction
{
	eLuxCoopBotAction_GoTo,
	eLuxCoopBotAction_Look,
	eLuxCoopBotAction_Follow,
	eLuxCoopBotAction_Wander,
	eLuxCoopBotAction_Flee,
	eLuxCoopBotAction_Wait,
	eLuxCoopBotAction_WaitUntil,
	eLuxCoopBotAction_Lantern,
	eLuxCoopBotAction_Run,
	eLuxCoopBotAction_Crouch,
	eLuxCoopBotAction_Script,
	eLuxCoopBotAction_Interact,
	eLuxCoopBotAction_End,
};

class cLuxCoopBotStep
{
public:
	eLuxCoopBotAction mType;
	tString msTarget;
	tString msFrom;
	tString msCode;
	tString msEnemyActive;
	float mfPartnerWithin;
	bool mbLookBack;
	float mfTime;
	float mfDist;
	bool mbOn;
	bool mbRun;
};

//----------------------------------------------

class cLuxCoopBot
{
public:
	cLuxCoopBot();
	~cLuxCoopBot();

	bool LoadScene(const tString& asFile, bool abHost);
	bool IsLoaded(){ return mvSteps.empty()==false; }
	bool IsRunning(){ return mbStarted && mbDone==false; }
	bool IsFinished(){ return mbDone; }

	void OnMapEnter();
	void Update(float afTimeStep, bool abPartnerReady, const cVector3f& avPartnerPos);

private:
	void StartStep();
	bool UpdateStep(float afTimeStep);
	void NextStep();

	//Movement and looking
	bool MoveTowards(const cVector3f& avGoal, float afArriveDist, float afTimeStep);
	bool PathTo(const cVector3f& avGoal);
	void StopMoving();
	void UpdateLook(float afTimeStep);
	void SetLookTarget(const cVector3f& avPos, float afTime);

	bool GetTargetPos(const tString& asTarget, cVector3f& avPos);
	bool GetNearestEnemy(cVector3f& avPos);
	bool PickWanderGoal(float afRadius, cVector3f& avGoal);
	bool PickFleeGoal(const cVector3f& avEnemyPos, cVector3f& avGoal);
	void SetupPathfinding();

	cVector3f GetFeetPos();

	std::vector<cLuxCoopBotStep> mvSteps;
	size_t mlStep;
	bool mbHost;
	bool mbStarted;
	bool mbDone;
	bool mbInvulnerable;
	bool mbKeepSanity;
	float mfStartDelay;
	float mfSceneTime;
	float mfLength;
	float mfStepTime;
	float mfStepCount;		//Per step countdown (re-path, pause)
	bool mbPausing;
	cVector3f mvStepGoal;

	cVector3f mvPartnerPos;
	bool mbPartnerReady;

	//Path
	cAINodeContainer *mpNodeContainer;
	cAStarHandler *mpAStar;
	cWorld *mpPathWorld;
	std::list<cVector3f> mlstPath;
	cVector3f mvLastPos;
	float mfStuckTime;
	bool mbRunning;
	bool mbCrouching;

	//Look
	cVector3f mvLookTarget;
	float mfLookTargetTime;	//Seconds left looking at mvLookTarget instead of where we walk
	cVector3f mvMoveDir;
	float mfGlanceCount;
	float mfNoiseT;
};

//----------------------------------------------

#endif // LUX_COOP_BOT_H
