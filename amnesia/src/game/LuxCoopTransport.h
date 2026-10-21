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

#ifndef LUX_COOP_TRANSPORT_H
#define LUX_COOP_TRANSPORT_H

//----------------------------------------------

#include "LuxBase.h"

//----------------------------------------------

//Both games must run the same version; it is checked in the handshake and tags Steam lobbies
static const int kCoopProtocolVersion = 5;

//----------------------------------------------

/**
 * What a transport reports back while it is updated.
 */
class iLuxCoopTransportListener
{
public:
	virtual ~iLuxCoopTransportListener(){}

	virtual void OnTransportConnected()=0;
	virtual void OnTransportDisconnected()=0;
	virtual void OnTransportPacket(const unsigned char *apData, size_t alSize)=0;
};

//----------------------------------------------

/**
 * How packets reach the partner: ENet over an IP address, or Steam lobbies and relays.
 * There is only ever one partner, so a transport has at most one peer.
 */
class iLuxCoopTransport
{
public:
	virtual ~iLuxCoopTransport(){}

	virtual bool StartHost()=0;
	//ENet takes "address:port", Steam a lobby id
	virtual bool StartClient(const tString& asTarget)=0;
	virtual void Stop()=0;

	virtual void Update(float afTimeStep, iLuxCoopTransportListener *apListener)=0;
	virtual void Flush()=0;

	virtual bool HasPeer()=0;
	virtual void Send(const unsigned char *apData, size_t alSize, int alChannel, bool abReliable)=0;
	virtual void DisconnectPeer()=0;

	//One line for the lobby window, in English
	virtual tString GetStatus()=0;
};

//----------------------------------------------

#endif // LUX_COOP_TRANSPORT_H
