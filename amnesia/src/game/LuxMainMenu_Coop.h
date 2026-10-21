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

#ifndef LUX_MAIN_MENU_COOP_H
#define LUX_MAIN_MENU_COOP_H

//----------------------------------------------

#include "LuxMainMenu.h"

//----------------------------------------------

/**
 * Co-op lobby: host a game or join one by address, then the host starts it for both.
 */
class cLuxMainMenu_Coop : public iLuxMainMenuWindow
{
public:
	cLuxMainMenu_Coop(cGuiSet *apGuiSet, cGuiSkin *apGuiSkin);
	~cLuxMainMenu_Coop();

	virtual void CreateGui();

	virtual void ExitPressed();

private:
	virtual void OnSetActive(bool abX);

	void UpdateStatus();
	tString GetEnteredName();
	int GetEnteredPort();

	////////////////////////
	// Properties
	cVector2f mvWindowSize;

	////////////////////////
	// Layout
	cWidgetTextBox *mpTextName;
	cWidgetTextBox *mpTextAddress;
	cWidgetTextBox *mpTextPort;
	cWidgetLabel *mpLStatus;

	cWidgetButton *mpHostButton;
	cWidgetButton *mpJoinButton;
	cWidgetButton *mpLeaveButton;
	cWidgetButton *mpStartButton;

	////////////////////////
	// Callbacks
	bool WindowOnUpdate(iWidget* apWidget, const cGuiMessageData& aData);
	kGuiCallbackDeclarationEnd(WindowOnUpdate);

	bool PressHost(iWidget* apWidget, const cGuiMessageData& aData);
	kGuiCallbackDeclarationEnd(PressHost);

	bool PressJoin(iWidget* apWidget, const cGuiMessageData& aData);
	kGuiCallbackDeclarationEnd(PressJoin);

	bool PressLeave(iWidget* apWidget, const cGuiMessageData& aData);
	kGuiCallbackDeclarationEnd(PressLeave);

	bool PressStart(iWidget* apWidget, const cGuiMessageData& aData);
	kGuiCallbackDeclarationEnd(PressStart);

	bool PressClose(iWidget* apWidget, const cGuiMessageData& aData);
	kGuiCallbackDeclarationEnd(PressClose);
};

//----------------------------------------------

#endif // LUX_MAIN_MENU_COOP_H
