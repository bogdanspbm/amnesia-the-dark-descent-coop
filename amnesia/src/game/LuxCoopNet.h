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

class iLuxProp;
class iLuxEntity;
class cLuxProp_Item;
class cLuxProp_Lamp;
class cLuxInventory_Item;
class iLuxEnemy;
class cLuxNode_PlayerStart;

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
	eLuxCoopMsg_BodyStates,
	eLuxCoopMsg_StartGame,
	eLuxCoopMsg_ChangeMap,
	eLuxCoopMsg_Pickup,
	eLuxCoopMsg_Interact,
	eLuxCoopMsg_LampLit,
	eLuxCoopMsg_UseItem,
	eLuxCoopMsg_Combine,
	eLuxCoopMsg_GiveItem,
	eLuxCoopMsg_DropItem,
	eLuxCoopMsg_Revive,
	eLuxCoopMsg_EnemyStates,
	eLuxCoopMsg_EnemySound,
	eLuxCoopMsg_Damage,
	eLuxCoopMsg_PropHealth,
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
	void AddShort(short alX){ AddByte((unsigned char)(alX & 0xff)); AddByte((unsigned char)((alX >> 8) & 0xff)); }
	void AddInt(int alX);
	void AddFloat(float afX);
	void AddVector3f(const cVector3f& avX){ AddFloat(avX.x); AddFloat(avX.y); AddFloat(avX.z); }
	void AddQuaternion(const cQuaternion& aqX){ AddFloat(aqX.w); AddFloat(aqX.v.x); AddFloat(aqX.v.y); AddFloat(aqX.v.z); }
	void AddString(const tString& asX);
	void AddBuffer(const cLuxCoopBuffer& aX){ mvData.insert(mvData.end(), aX.mvData.begin(), aX.mvData.end()); }

	unsigned char GetByte();
	short GetShort(){ unsigned int lLow = GetByte(); unsigned int lHigh = GetByte(); return (short)(lLow | (lHigh << 8)); }
	int GetInt();
	float GetFloat();
	cVector3f GetVector3f(){ float x = GetFloat(); float y = GetFloat(); float z = GetFloat(); return cVector3f(x,y,z); }
	tString GetString();
	cQuaternion GetQuaternion(){ float w = GetFloat(); cVector3f v = GetVector3f(); return cQuaternion(w, v.x, v.y, v.z); }

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
 * Last body transform sent to the partner, so only bodies that moved are sent again.
 */
class cLuxCoopSentBody
{
public:
	cLuxCoopSentBody() : mfTime(-1000.0f), mlRestSendsLeft(0){}

	cVector3f mvPos;
	cQuaternion mqRot;
	float mfTime;
	int mlRestSendsLeft;	//A body that came to rest is sent a couple of times more, flagged as resting
};

/**
 * A body waiting for its turn in the per-tick send budget.
 */
class cLuxCoopBodyCandidate
{
public:
	iLuxProp *mpProp;
	int mlBodyIdx;
	float mfPriority;
	bool mbRest;
};

//Bodies are keyed by entity name and body index; ids of entities created at runtime can differ between the two games.
//On the wire the name is replaced by a 32 bit hash of it.
typedef std::pair<tString, int> tLuxCoopBodyKey;
typedef std::map<tLuxCoopBodyKey, cLuxCoopSentBody> tLuxCoopSentBodyMap;
typedef std::map<tLuxCoopBodyKey, float> tLuxCoopOwnedBodyMap;
typedef std::map<unsigned int, tString> tLuxCoopNameHashMap;

//----------------------------------------------

//Partner animations, looked up in the .ent by these names (see gsCoopAvatarAnimNames)
enum eLuxCoopAvatarAnim
{
	eLuxCoopAvatarAnim_Idle,
	eLuxCoopAvatarAnim_Walk,
	eLuxCoopAvatarAnim_Run,
	eLuxCoopAvatarAnim_CrouchIdle,
	eLuxCoopAvatarAnim_CrouchWalk,
	eLuxCoopAvatarAnim_IdleLantern,
	eLuxCoopAvatarAnim_WalkLantern,
	eLuxCoopAvatarAnim_RunLantern,
	eLuxCoopAvatarAnim_CrouchIdleLantern,
	eLuxCoopAvatarAnim_CrouchWalkLantern,
	eLuxCoopAvatarAnim_Hurt,
	eLuxCoopAvatarAnim_Downed,
	eLuxCoopAvatarAnim_Death,

	eLuxCoopAvatarAnim_LastEnum,
};

/**
 * What the partner looks like, from coop.cfg.
 */
class cLuxCoopAvatarSettings
{
public:
	tString msModel;
	tString msModelDir;			//Folder with the mod's own partner model, searched for files like the game's folders
	float mfYawOffset;
	tString msLanternMesh;		//Mesh hung on msLanternBone while the lantern is out, empty for none
	tString msLanternBone;
	tString msLanternOffset;	//Lantern light position relative to the bone, or to the feet without a bone, "x y z"
};

/**
 * The partner as seen in the local world: a mesh plus a lantern light,
 * animated from the partner's movement, stance, lantern and health.
 */
class cLuxCoopAvatar
{
public:
	cLuxCoopAvatar();

	void Create(cLuxMap *apMap, const cLuxCoopAvatarSettings& aSettings);
	void Destroy(cLuxMap *apMap);
	void OnWorldDestroyed();

	void SetTarget(const cVector3f& avFeetPos, float afYaw, bool abLanternOn, bool abInMap, bool abDowned=false, bool abDead=false, bool abCrouching=false, int alHurtCount=-1);
	void Update(float afTimeStep);

	bool IsCreated(){ return mpMesh != NULL; }
	bool IsShown(){ return mpMesh != NULL && mbHasState && mbVisible; }
	const cVector3f& GetPosition(){ return mvPos; }
	iCharacterBody* GetCharacterBody(){ return mpBody; }

private:
	void ApplyTransform();
	void ReadEntitySettings(cXmlElement *apElem, cLuxCoopAvatarSettings &aSettings);
	void LoadEntityAnimations(cWorld *apWorld, cXmlElement *apAnimationsElem, const tString& asEntPath);
	void UpdateAnimation(float afTimeStep, float afSpeed);
	cAnimationState* GetLocomotionAnim(float afSpeed);
	void PlayAnimState(cAnimationState *apState, float afFadeTime=0.25f);
	cAnimationState* GetAnim(eLuxCoopAvatarAnim aAnim){ return mvAnims[aAnim]; }

	cMeshEntity *mpMesh;
	cAnimationState* mvAnims[eLuxCoopAvatarAnim_LastEnum];
	cAnimationState *mpAnimCurrent;
	bool mbHasLyingAnim;
	float mfYawOffset;
	float mfSpeed;
	float mfBobCount;
	float mfHurtCount;
	cLightPoint *mpLight;
	cMeshEntity *mpLanternMesh;
	cBoneState *mpLanternBone;
	cVector3f mvLanternOffset;
	iCharacterBody *mpBody;

	cVector3f mvPos;
	cVector3f mvTargetPos;
	float mfYaw;
	float mfTargetYaw;
	bool mbLanternOn;
	bool mbDowned;
	bool mbDead;
	bool mbCrouching;
	int mlHurtCount;
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
	void SaveMainConfig();

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

	/////////////////////////
	// Player hooks
	iPhysicsBody* GetPartnerCollideBody();
	cVector3f AdjustStartPos(const cVector3f& avFeetPos, float afYaw);
	void OnLocalChangeMap(const tString& asMapName, const tString& asStartPos, const tString& asStartSound, const tString& asEndSound);
	void OnCheckPoint(cLuxNode_PlayerStart *apStartNode);

	/////////////////////////
	// Shared world: local actions replayed in the partner's game
	void OnLocalPickup(cLuxProp_Item *apItem);
	void OnLocalInteract(iLuxEntity *apEntity);
	void OnLocalLampLit(cLuxProp_Lamp *apLamp);
	void OnLocalUseItem(const tString& asItem, const tString& asEntity);
	void OnLocalCombine(const tString& asItemA, const tString& asItemB);
	void GiveOrDropItem(cLuxInventory_Item *apItem);

	/////////////////////////
	// Downed and revive
	bool TryStartDowned();
	bool IsDowned(){ return mbDowned; }

	/////////////////////////
	// Enemies: the host runs them and they may hunt either player
	bool IsEnemyMirror(){ return mMode == eLuxCoopMode_Client && IsConnected(); }
	iCharacterBody* GetPartnerCharBody();
	bool IsPartnerDown(){ return mbPartnerDowned || mbPartnerDead; }
	bool GetPartnerLanternOn(){ return mbPartnerLanternOn; }
	bool GetPartnerCrouching(){ return mbPartnerCrouching; }
	float GetPartnerLightLevel(){ return mfPartnerLightLevel; }
	float GetPartnerAvgSpeed(){ return mfPartnerAvgSpeed; }
	void OnEnemySound(iLuxEnemy *apEnemy, const tString& asSound);
	void DamagePartner(float afAmount, int alStrength, eLuxDamageType aType, bool abLethal);

	//Story items go to both inventories; consumables stay with whoever picks them up
	static bool IsSharedItemType(eLuxItemType aType);

	//True while a partner's action is being replayed, so it is not sent back
	bool IsApplyingRemote(){ return mbApplyingRemote; }

	/////////////////////////
	// Prop health: the host decides when things break
	bool IgnoreLocalPropDamage();
	void OnPropHealthChange(iLuxProp *apProp, float afOldHealth);

	/////////////////////////
	// Lobby
	bool Host(const tString& asName, int alPort);
	bool Join(const tString& asName, const tString& asAddress, int alPort);
	void Leave();
	void StartCoopGame();

	const tString& GetName(){ return msName; }
	const tString& GetAddress(){ return msAddress; }
	int GetPort(){ return mlPort; }

private:
	bool StartNetwork();
	void StopNetwork();

	void PollEvents();
	void HandlePacket(_ENetPacket *apPacket);
	void HandleHello(cLuxCoopBuffer &aBuffer);
	void HandlePlayerState(cLuxCoopBuffer &aBuffer);
	void HandleMapInfo(cLuxCoopBuffer &aBuffer);
	void HandleBodyStates(cLuxCoopBuffer &aBuffer);
	void HandleStartGame(cLuxCoopBuffer &aBuffer);
	void HandleChangeMap(cLuxCoopBuffer &aBuffer);
	void HandlePickup(cLuxCoopBuffer &aBuffer);
	void HandleInteract(cLuxCoopBuffer &aBuffer);
	void HandleLampLit(cLuxCoopBuffer &aBuffer);
	void HandleUseItem(cLuxCoopBuffer &aBuffer);
	void HandleCombine(cLuxCoopBuffer &aBuffer);
	void HandleGiveItem(cLuxCoopBuffer &aBuffer);
	void HandleDropItem(cLuxCoopBuffer &aBuffer);
	void HandleRevive(cLuxCoopBuffer &aBuffer);
	void HandleEnemyStates(cLuxCoopBuffer &aBuffer);
	void HandleEnemySound(cLuxCoopBuffer &aBuffer);
	void HandleDamage(cLuxCoopBuffer &aBuffer);
	void HandlePropHealth(cLuxCoopBuffer &aBuffer);
	void SendEnemyStates();

	void UpdateDowned(float afTimeStep);
	void EndDowned();

	void ShowMessage(const tWString& asText);
	tWString GetItemDisplayName(eLuxItemType aType, const tString& asSubType);

	void SaveLobbySettings();
	tWString GetConfigFile();
	void StartLocalGame();

	void SendHello();
	void SendPlayerState();
	void SendMapInfo();
	void SendBodyStates();
	void SendOwnedBodyStates();
	void SendBodyCandidates(std::vector<cLuxCoopBodyCandidate> &avCandidates);
	bool IsClientOwned(const tLuxCoopBodyKey& aKey);
	void AddBodyEntry(cLuxCoopBuffer &aBuffer, iLuxProp *apProp, int alBodyIdx, bool abRest);
	void ApplyBodyState(iPhysicsBody *apBody, const cVector3f& avPos, const cQuaternion& aqRot, const cVector3f& avLinVel, const cVector3f& avAngVel, bool abRest);
	iPhysicsBody* GetInteractBody();
	void SendBodyPacket(const tString& asMap, const cLuxCoopBuffer &aEntries, int alCount);

	static unsigned int HashName(const tString& asName);
	iLuxProp* GetPropByNameHash(unsigned int alHash);
	void Send(const cLuxCoopBuffer &aBuffer, int alChannel, bool abReliable);

	tString GetCurrentMapFile();

	eLuxCoopMode mMode;
	tString msAddress;
	int mlPort;
	tString msName;
	cLuxCoopAvatarSettings mAvatarSettings;
	bool mbAvatarDirAdded;
	int mlHurtCount;
	float mfLastHealth;
	float mfStateSendInterval;

	_ENetHost *mpHost;
	_ENetPeer *mpPeer;
	bool mbEnetInitialized;
	bool mbHandshakeDone;
	float mfStateSendCount;
	float mfReconnectCount;
	float mfBodySendCount;
	float mfTime;
	bool mbApplyingRemote;
	int mlDropCount;

	bool mbDowned;
	bool mbForceDeath;
	float mfDownedTime;
	float mfDownedMessageCount;
	float mfReviveCount;
	bool mbPartnerDowned;
	bool mbPartnerDead;
	bool mbPartnerLanternOn;
	bool mbPartnerCrouching;
	float mfPartnerLightLevel;
	float mfPartnerAvgSpeed;
	float mfPartnerLastStateTime;
	cVector3f mvPartnerLastPos;
	float mfEnemySendCount;
	bool mbSleepOverridden;
	bool mbSleepWhenOutOfFocus;
	tLuxCoopSentBodyMap m_mapSentBodies;
	tLuxCoopOwnedBodyMap m_mapClientOwnedBodies;
	tLuxCoopNameHashMap m_mapPropNameHashes;
	float mfPropNameHashTime;

	tString msPartnerName;
	tString msPartnerMap;

	cLuxCoopAvatar mAvatar;
};

//----------------------------------------------

#endif // LUX_COOP_NET_H
