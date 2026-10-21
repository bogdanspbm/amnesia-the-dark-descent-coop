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

#ifndef LUX_COOP_TRANSPORT_ENET_H
#define LUX_COOP_TRANSPORT_ENET_H

//----------------------------------------------

#include "LuxCoopTransport.h"

//----------------------------------------------

struct _ENetHost;
struct _ENetPeer;

//----------------------------------------------

/**
 * Direct connection by IP over UDP. The host listens on a port, the client keeps trying until it answers.
 */
class cLuxCoopTransport_ENet : public iLuxCoopTransport
{
public:
	cLuxCoopTransport_ENet(int alPort, int alChannelNum);
	~cLuxCoopTransport_ENet();

	bool StartHost();
	bool StartClient(const tString& asTarget);
	void Stop();

	void Update(float afTimeStep, iLuxCoopTransportListener *apListener);
	void Flush();

	bool HasPeer(){ return mpPeer != NULL; }
	void Send(const unsigned char *apData, size_t alSize, int alChannel, bool abReliable);
	void DisconnectPeer();

	tString GetStatus();

private:
	bool Init();

	int mlPort;
	int mlChannelNum;
	tString msAddress;
	bool mbHost;
	bool mbInitialized;
	float mfReconnectCount;

	_ENetHost *mpHost;
	_ENetPeer *mpPeer;
};

//----------------------------------------------

#endif // LUX_COOP_TRANSPORT_ENET_H
