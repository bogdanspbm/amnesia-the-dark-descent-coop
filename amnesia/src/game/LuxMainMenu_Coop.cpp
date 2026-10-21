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

#include "LuxMainMenu_Coop.h"
#include "LuxBase.h"
#include "LuxCoopNet.h"
#include "LuxCoopTransport_Steam.h"

//-----------------------------------------------------------------------

//Text comes from the "Coop" category of the language file when it has one, English otherwise
static tWString CoopText(const tString& asEntry, const tString& asEnglish)
{
	const tWString& sText = kTranslate("Coop", asEntry);
	return sText.empty() ? cString::To16Char(asEnglish) : sText;
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// CONSTRUCTORS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cLuxMainMenu_Coop::cLuxMainMenu_Coop(cGuiSet *apGuiSet, cGuiSkin *apGuiSkin) : iLuxMainMenuWindow(apGuiSet, apGuiSkin)
{
	mvWindowSize = cVector2f(520, 520);

	mpTextName = NULL;
	mpTextAddress = NULL;
	mpTextPort = NULL;
	mpLStatus = NULL;
	mpHostButton = NULL;
	mpJoinButton = NULL;
	mpLeaveButton = NULL;
	mpStartButton = NULL;
	mpSteamPublicButton = NULL;
	mpSteamFriendsButton = NULL;
	mpSteamFindButton = NULL;
	mpSteamInviteButton = NULL;
	for(int i=0; i<kCoopLobbyListSize; ++i) mpLobbyButtons[i] = NULL;
	mbSearched = false;
}

//-----------------------------------------------------------------------

cLuxMainMenu_Coop::~cLuxMainMenu_Coop()
{
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

void cLuxMainMenu_Coop::CreateGui()
{
	cLuxCoopNet *pNet = gpBase->mpCoopNet;

	//////////////////////////
	//Window
	mpWindow = mpGuiSet->CreateWidgetWindow(eWidgetWindowButtonFlag_None, cVector3f(0,0,5), mvWindowSize, CoopText("Title", "Co-op"));
	mpWindow->AddCallback(eGuiMessage_OnUpdate, this, kGuiCallback(WindowOnUpdate));

	float fBorderSize = 15;
	float fLabelWidth = 130;
	float fFieldWidth = mvWindowSize.x - fLabelWidth - fBorderSize*2;
	float fRowHeight = 28;
	float fButtonSepp = 6;
	float fInnerWidth = mvWindowSize.x - fBorderSize*2;
	float fHalfWidth = (fInnerWidth - fButtonSepp) / 2.0f;
	cVector3f vPos(fBorderSize, 40, 0.1f);

	//////////////////////////
	//Name
	mpGuiSet->CreateWidgetLabel(vPos, cVector2f(fLabelWidth, 20), CoopText("Name", "Your name"), mpWindow);
	mpTextName = mpGuiSet->CreateWidgetTextBox(vPos + cVector3f(fLabelWidth,0,0), cVector2f(fFieldWidth, 20), cString::To16Char(pNet->GetName()), mpWindow);
	mpTextName->SetMaxTextLength(24);
	vPos.y += fRowHeight + 4;

	//////////////////////////
	//Steam: host, find, invite, and the lobbies found
	mpGuiSet->CreateWidgetLabel(vPos, cVector2f(fInnerWidth, 20), CoopText("SteamHeader", "Steam"), mpWindow);
	vPos.y += 22;

	mpSteamPublicButton = mpGuiSet->CreateWidgetButton(vPos, cVector2f(fHalfWidth, 28), CoopText("SteamHostPublic", "Host public lobby"), mpWindow);
	mpSteamPublicButton->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(PressSteamPublic));
	mpSteamFriendsButton = mpGuiSet->CreateWidgetButton(vPos + cVector3f(fHalfWidth + fButtonSepp,0,0), cVector2f(fHalfWidth, 28), CoopText("SteamHostFriends", "Host for friends"), mpWindow);
	mpSteamFriendsButton->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(PressSteamFriends));
	vPos.y += 28 + fButtonSepp;

	mpSteamFindButton = mpGuiSet->CreateWidgetButton(vPos, cVector2f(fHalfWidth, 28), CoopText("SteamFind", "Find games"), mpWindow);
	mpSteamFindButton->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(PressSteamFind));
	mpSteamInviteButton = mpGuiSet->CreateWidgetButton(vPos + cVector3f(fHalfWidth + fButtonSepp,0,0), cVector2f(fHalfWidth, 28), CoopText("SteamInvite", "Invite friend"), mpWindow);
	mpSteamInviteButton->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(PressSteamInvite));
	vPos.y += 28 + fButtonSepp;

	for(int i=0; i<kCoopLobbyListSize; ++i)
	{
		mpLobbyButtons[i] = mpGuiSet->CreateWidgetButton(vPos, cVector2f(fInnerWidth, 22), _W(""), mpWindow);
		mpLobbyButtons[i]->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(PressLobby));
		mpLobbyButtons[i]->SetVisible(false);
		vPos.y += 22 + 3;
	}
	vPos.y += 6;

	//////////////////////////
	//Direct connection by IP
	mpGuiSet->CreateWidgetLabel(vPos, cVector2f(fInnerWidth, 20), CoopText("DirectHeader", "Direct connection (LAN, Radmin VPN, ZeroTier)"), mpWindow);
	vPos.y += 22;

	mpGuiSet->CreateWidgetLabel(vPos, cVector2f(fLabelWidth, 20), CoopText("Address", "Host IP"), mpWindow);
	mpTextAddress = mpGuiSet->CreateWidgetTextBox(vPos + cVector3f(fLabelWidth,0,0), cVector2f(fFieldWidth - 100, 20), cString::To16Char(pNet->GetAddress()), mpWindow);
	mpTextAddress->SetMaxTextLength(64);
	mpTextPort = mpGuiSet->CreateWidgetTextBox(vPos + cVector3f(fLabelWidth + fFieldWidth - 90,0,0), cVector2f(90, 20), cString::To16Char(cString::ToString(pNet->GetPort())), mpWindow);
	mpTextPort->SetMaxTextLength(5);
	vPos.y += fRowHeight;

	float fButtonWidth = (fInnerWidth - fButtonSepp*2) / 3.0f;
	mpHostButton = mpGuiSet->CreateWidgetButton(vPos, cVector2f(fButtonWidth, 28), CoopText("Host", "Host by IP"), mpWindow);
	mpHostButton->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(PressHost));

	mpJoinButton = mpGuiSet->CreateWidgetButton(vPos + cVector3f(fButtonWidth + fButtonSepp,0,0), cVector2f(fButtonWidth, 28), CoopText("Join", "Join by IP"), mpWindow);
	mpJoinButton->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(PressJoin));

	mpLeaveButton = mpGuiSet->CreateWidgetButton(vPos + cVector3f((fButtonWidth + fButtonSepp)*2,0,0), cVector2f(fButtonWidth, 28), CoopText("Leave", "Leave"), mpWindow);
	mpLeaveButton->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(PressLeave));
	vPos.y += 28 + fButtonSepp*2;

	//////////////////////////
	//Status
	cVector2f vStatusSize(fInnerWidth, mvWindowSize.y - vPos.y - 50);
	mpLStatus = mpGuiSet->CreateWidgetLabel(vPos, vStatusSize, _W(""), mpWindow);
	mpLStatus->SetWordWrap(true);
	mpLStatus->SetDrawBackGround(true);
	mpLStatus->SetBackGroundColor(cColor(0, 0.5f));
	mpLStatus->SetDefaultFontColor(cColor(1, 1));

	//////////////////////////
	//Start / Close
	vPos.y = mvWindowSize.y - 40;
	float fBottomWidth = 150;

	cWidgetButton *pCloseButton = mpGuiSet->CreateWidgetButton(cVector3f(fBorderSize, vPos.y, 0.1f), cVector2f(fBottomWidth, 30), kTranslate("Global", "Cancel"), mpWindow);
	pCloseButton->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(PressClose));

	mpStartButton = mpGuiSet->CreateWidgetButton(cVector3f(mvWindowSize.x - fBorderSize - fBottomWidth, vPos.y, 0.1f), cVector2f(fBottomWidth, 30), CoopText("Start", "Start game"), mpWindow);
	mpStartButton->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(PressStart));

	mpContinueButton = mpGuiSet->CreateWidgetButton(cVector3f((mvWindowSize.x - fBottomWidth)*0.5f, vPos.y, 0.1f), cVector2f(fBottomWidth, 30), CoopText("Continue", "Continue together"), mpWindow);
	mpContinueButton->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(PressContinue));

	UpdateStatus();
}

//-----------------------------------------------------------------------

void cLuxMainMenu_Coop::ExitPressed()
{
	if(mpGuiSet->PopUpIsActive()) return;

	gpBase->mpMainMenu->SetWindowActive(eLuxMainMenuWindow_LastEnum);
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

void cLuxMainMenu_Coop::OnSetActive(bool abX)
{
	if(abX) UpdateStatus();
}

//-----------------------------------------------------------------------

void cLuxMainMenu_Coop::UpdateStatus()
{
	if(mpLStatus==NULL) return;

	cLuxCoopNet *pNet = gpBase->mpCoopNet;
	bool bSteam = pNet->GetNetType() == eLuxCoopNetType_Steam;
	tString sStatus;

	if(pNet->IsActive()==false)
	{
		sStatus = "Not connected. Host a Steam lobby or find one, or connect directly by IP.";
		tString sError = pNet->GetTransportStatus();
		if(sError != "") sStatus += "\n" + sError;
	}
	else if(pNet->IsConnected())
	{
		if(pNet->GetSaveSendProgress() >= 0)	sStatus = "Sending your save to '" + pNet->GetPartnerName() + "': " + cString::ToString((int)(pNet->GetSaveSendProgress()*100)) + "%";
		else if(pNet->IsHost())	sStatus = "Partner '" + pNet->GetPartnerName() + "' joined. Press \"Start game\" for a new game, or \"Continue together\" to load your newest save. Your partner keeps their own inventory.";
		else				sStatus = "Connected to '" + pNet->GetPartnerName() + "'. Waiting for the host to start the game...";
	}
	else
	{
		sStatus = pNet->GetTransportStatus();
		if(sStatus == "") sStatus = pNet->IsHost() ? "Waiting for a partner..." : "Connecting...";
	}

	if(pNet->GetLobbyNotice() != "") sStatus += "\n" + pNet->GetLobbyNotice();
	//Statuses carry Steam names, which are UTF-8
	mpLStatus->SetText(cString::UTF8ToWChar(sStatus));

	bool bActive = pNet->IsActive();
	mpHostButton->SetEnabled(bActive==false);
	mpJoinButton->SetEnabled(bActive==false);
	mpLeaveButton->SetEnabled(bActive);
	mpTextName->SetEnabled(bActive==false);
	mpTextAddress->SetEnabled(bActive==false);
	mpTextPort->SetEnabled(bActive==false);
	mpStartButton->SetEnabled(pNet->IsHost() && pNet->IsConnected() && pNet->GetSaveSendProgress() < 0);
	mpContinueButton->SetEnabled(pNet->CanContinueCoopGame());

	cLuxCoopTransport_Steam *pSteam = pNet->GetSteam();
	mpSteamPublicButton->SetEnabled(bActive==false);
	mpSteamFriendsButton->SetEnabled(bActive==false);
	mpSteamFindButton->SetEnabled(bActive==false && (pSteam==NULL || pSteam->IsListingLobbies()==false));
	mpSteamInviteButton->SetEnabled(bActive && bSteam && pNet->IsHost() && pSteam && pSteam->InLobby());

	UpdateLobbyList();
}

void cLuxMainMenu_Coop::UpdateLobbyList()
{
	cLuxCoopNet *pNet = gpBase->mpCoopNet;
	cLuxCoopTransport_Steam *pSteam = pNet->GetSteam();
	bool bShow = mbSearched && pNet->IsActive()==false && pSteam && pSteam->IsAvailable();

	const tLuxCoopLobbyInfoVec *pLobbies = bShow ? &pSteam->GetLobbies() : NULL;
	for(int i=0; i<kCoopLobbyListSize; ++i)
	{
		msLobbyIds[i] = "";
		if(pLobbies && i < (int)pLobbies->size())
		{
			const cLuxCoopLobbyInfo &lobby = (*pLobbies)[i];
			msLobbyIds[i] = lobby.msId;
			mpLobbyButtons[i]->SetText(cString::UTF8ToWChar(lobby.msName + "  (" + cString::ToString(lobby.mlMembers) + "/" + cString::ToString(lobby.mlMaxMembers) + ")"));
			mpLobbyButtons[i]->SetVisible(true);
		}
		else if(i==0 && bShow)
		{
			//One line saying the search is running or found nothing
			mpLobbyButtons[i]->SetText(pSteam->IsListingLobbies() ? CoopText("SteamSearching", "Searching...") : CoopText("SteamNoGames", "No games found. Ask your friend to host, then search again."));
			mpLobbyButtons[i]->SetVisible(true);
		}
		else
		{
			mpLobbyButtons[i]->SetVisible(false);
		}
	}
}

//-----------------------------------------------------------------------

tString cLuxMainMenu_Coop::GetEnteredName()
{
	tString sName = cString::To8Char(mpTextName->GetText());
	return sName.empty() ? "Player" : sName;
}

int cLuxMainMenu_Coop::GetEnteredPort()
{
	int lPort = cString::ToInt(cString::To8Char(mpTextPort->GetText()).c_str(), 27960);
	return (lPort > 0 && lPort < 65536) ? lPort : 27960;
}

//-----------------------------------------------------------------------

bool cLuxMainMenu_Coop::WindowOnUpdate(iWidget* apWidget, const cGuiMessageData& aData)
{
	UpdateStatus();
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxMainMenu_Coop, WindowOnUpdate);

//-----------------------------------------------------------------------

bool cLuxMainMenu_Coop::PressHost(iWidget* apWidget, const cGuiMessageData& aData)
{
	gpBase->mpCoopNet->Host(GetEnteredName(), GetEnteredPort());
	UpdateStatus();
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxMainMenu_Coop, PressHost);

//-----------------------------------------------------------------------

bool cLuxMainMenu_Coop::PressJoin(iWidget* apWidget, const cGuiMessageData& aData)
{
	tString sAddress = cString::To8Char(mpTextAddress->GetText());
	if(sAddress.empty()) return true;

	gpBase->mpCoopNet->Join(GetEnteredName(), sAddress, GetEnteredPort());
	UpdateStatus();
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxMainMenu_Coop, PressJoin);

//-----------------------------------------------------------------------

bool cLuxMainMenu_Coop::PressLeave(iWidget* apWidget, const cGuiMessageData& aData)
{
	gpBase->mpCoopNet->Leave();
	UpdateStatus();
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxMainMenu_Coop, PressLeave);

//-----------------------------------------------------------------------

bool cLuxMainMenu_Coop::PressSteamPublic(iWidget* apWidget, const cGuiMessageData& aData)
{
	gpBase->mpCoopNet->HostSteam(GetEnteredName(), true);
	UpdateStatus();
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxMainMenu_Coop, PressSteamPublic);

bool cLuxMainMenu_Coop::PressSteamFriends(iWidget* apWidget, const cGuiMessageData& aData)
{
	gpBase->mpCoopNet->HostSteam(GetEnteredName(), false);
	UpdateStatus();
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxMainMenu_Coop, PressSteamFriends);

bool cLuxMainMenu_Coop::PressSteamFind(iWidget* apWidget, const cGuiMessageData& aData)
{
	cLuxCoopNet *pNet = gpBase->mpCoopNet;
	if(pNet->SteamAvailable()) pNet->GetSteam()->RequestLobbyList();
	mbSearched = true;
	UpdateStatus();
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxMainMenu_Coop, PressSteamFind);

bool cLuxMainMenu_Coop::PressSteamInvite(iWidget* apWidget, const cGuiMessageData& aData)
{
	cLuxCoopTransport_Steam *pSteam = gpBase->mpCoopNet->GetSteam();
	if(pSteam) pSteam->OpenInviteDialog();
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxMainMenu_Coop, PressSteamInvite);

bool cLuxMainMenu_Coop::PressLobby(iWidget* apWidget, const cGuiMessageData& aData)
{
	for(int i=0; i<kCoopLobbyListSize; ++i)
	{
		if(apWidget != mpLobbyButtons[i] || msLobbyIds[i] == "") continue;

		gpBase->mpCoopNet->JoinSteamLobby(GetEnteredName(), msLobbyIds[i]);
		mbSearched = false;
		UpdateStatus();
		break;
	}
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxMainMenu_Coop, PressLobby);

//-----------------------------------------------------------------------

bool cLuxMainMenu_Coop::PressStart(iWidget* apWidget, const cGuiMessageData& aData)
{
	gpBase->mpCoopNet->StartCoopGame();
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxMainMenu_Coop, PressStart);

bool cLuxMainMenu_Coop::PressContinue(iWidget* apWidget, const cGuiMessageData& aData)
{
	gpBase->mpCoopNet->ContinueCoopGame();
	UpdateStatus();
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxMainMenu_Coop, PressContinue);

//-----------------------------------------------------------------------

bool cLuxMainMenu_Coop::PressClose(iWidget* apWidget, const cGuiMessageData& aData)
{
	//Closing the lobby keeps the connection, so a joined partner can wait in the main menu
	gpBase->mpMainMenu->SetWindowActive(eLuxMainMenuWindow_LastEnum);
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxMainMenu_Coop, PressClose);

//-----------------------------------------------------------------------
