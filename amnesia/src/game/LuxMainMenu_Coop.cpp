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
	mvWindowSize = cVector2f(460, 300);

	mpTextName = NULL;
	mpTextAddress = NULL;
	mpTextPort = NULL;
	mpLStatus = NULL;
	mpHostButton = NULL;
	mpJoinButton = NULL;
	mpLeaveButton = NULL;
	mpStartButton = NULL;
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
	cVector3f vPos(fBorderSize, 40, 0.1f);

	//////////////////////////
	//Name
	mpGuiSet->CreateWidgetLabel(vPos, cVector2f(fLabelWidth, 20), CoopText("Name", "Your name"), mpWindow);
	mpTextName = mpGuiSet->CreateWidgetTextBox(vPos + cVector3f(fLabelWidth,0,0), cVector2f(fFieldWidth, 20), cString::To16Char(pNet->GetName()), mpWindow);
	mpTextName->SetMaxTextLength(24);
	vPos.y += fRowHeight;

	//////////////////////////
	//Address and port
	mpGuiSet->CreateWidgetLabel(vPos, cVector2f(fLabelWidth, 20), CoopText("Address", "Host IP"), mpWindow);
	mpTextAddress = mpGuiSet->CreateWidgetTextBox(vPos + cVector3f(fLabelWidth,0,0), cVector2f(fFieldWidth, 20), cString::To16Char(pNet->GetAddress()), mpWindow);
	mpTextAddress->SetMaxTextLength(64);
	vPos.y += fRowHeight;

	mpGuiSet->CreateWidgetLabel(vPos, cVector2f(fLabelWidth, 20), CoopText("Port", "Port"), mpWindow);
	mpTextPort = mpGuiSet->CreateWidgetTextBox(vPos + cVector3f(fLabelWidth,0,0), cVector2f(90, 20), cString::To16Char(cString::ToString(pNet->GetPort())), mpWindow);
	mpTextPort->SetMaxTextLength(5);
	vPos.y += fRowHeight + 6;

	//////////////////////////
	//Host / Join / Leave
	float fButtonSepp = 6;
	float fButtonWidth = (mvWindowSize.x - fBorderSize*2 - fButtonSepp*2) / 3.0f;

	mpHostButton = mpGuiSet->CreateWidgetButton(vPos, cVector2f(fButtonWidth, 30), CoopText("Host", "Host game"), mpWindow);
	mpHostButton->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(PressHost));

	mpJoinButton = mpGuiSet->CreateWidgetButton(vPos + cVector3f(fButtonWidth + fButtonSepp,0,0), cVector2f(fButtonWidth, 30), CoopText("Join", "Join"), mpWindow);
	mpJoinButton->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(PressJoin));

	mpLeaveButton = mpGuiSet->CreateWidgetButton(vPos + cVector3f((fButtonWidth + fButtonSepp)*2,0,0), cVector2f(fButtonWidth, 30), CoopText("Leave", "Leave"), mpWindow);
	mpLeaveButton->AddCallback(eGuiMessage_ButtonPressed, this, kGuiCallback(PressLeave));
	vPos.y += 30 + fButtonSepp*2;

	//////////////////////////
	//Status
	cVector2f vStatusSize(mvWindowSize.x - fBorderSize*2, mvWindowSize.y - vPos.y - 50);
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
	tString sStatus;

	if(pNet->IsActive()==false)
	{
		sStatus = "Not connected. Press \"Host game\" to wait for a partner, or enter the host's IP and press \"Join\".";
	}
	else if(pNet->IsHost())
	{
		if(pNet->IsConnected())
			sStatus = "Partner '" + pNet->GetPartnerName() + "' joined. Press \"Start game\" to begin together.";
		else
			sStatus = "Hosting on port " + cString::ToString(pNet->GetPort()) + ". Waiting for a partner to join...\n"
					  "Your partner needs your IP (LAN, Radmin VPN or ZeroTier). Allow Amnesia through the Windows firewall.";
	}
	else
	{
		if(pNet->IsConnected())
			sStatus = "Connected to '" + pNet->GetPartnerName() + "'. Waiting for the host to start the game...";
		else
			sStatus = "Connecting to " + pNet->GetAddress() + ":" + cString::ToString(pNet->GetPort()) + "...";
	}

	mpLStatus->SetText(cString::To16Char(sStatus));

	bool bActive = pNet->IsActive();
	mpHostButton->SetEnabled(bActive==false);
	mpJoinButton->SetEnabled(bActive==false);
	mpLeaveButton->SetEnabled(bActive);
	mpTextName->SetEnabled(bActive==false);
	mpTextAddress->SetEnabled(bActive==false);
	mpTextPort->SetEnabled(bActive==false);
	mpStartButton->SetEnabled(pNet->IsHost() && pNet->IsConnected());
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

bool cLuxMainMenu_Coop::PressStart(iWidget* apWidget, const cGuiMessageData& aData)
{
	gpBase->mpCoopNet->StartCoopGame();
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxMainMenu_Coop, PressStart);

//-----------------------------------------------------------------------

bool cLuxMainMenu_Coop::PressClose(iWidget* apWidget, const cGuiMessageData& aData)
{
	//Closing the lobby keeps the connection, so a joined partner can wait in the main menu
	gpBase->mpMainMenu->SetWindowActive(eLuxMainMenuWindow_LastEnum);
	return true;
}
kGuiCallbackDeclaredFuncEnd(cLuxMainMenu_Coop, PressClose);

//-----------------------------------------------------------------------
