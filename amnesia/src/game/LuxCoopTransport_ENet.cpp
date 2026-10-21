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

#include "LuxCoopTransport_ENet.h"

#include <enet/enet.h>

//enet pulls in windows.h, whose macros rename functions all over the engine
#undef PlaySound

//-----------------------------------------------------------------------

static const float kENetReconnectTime = 3.0f;

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// CONSTRUCTORS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cLuxCoopTransport_ENet::cLuxCoopTransport_ENet(int alPort, int alChannelNum)
{
	mlPort = alPort;
	mlChannelNum = alChannelNum;
	mbHost = false;
	mbInitialized = false;
	mfReconnectCount = 0;
	mpHost = NULL;
	mpPeer = NULL;
}

cLuxCoopTransport_ENet::~cLuxCoopTransport_ENet()
{
	Stop();
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

bool cLuxCoopTransport_ENet::StartHost()
{
	if(Init()==false) return false;
	mbHost = true;

	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = (enet_uint16)mlPort;
	mpHost = enet_host_create(&address, 1, mlChannelNum, 0, 0);
	if(mpHost==NULL)
	{
		Error("Coop: could not listen on port %d\n", mlPort);
		return false;
	}
	Log("Coop: hosting on port %d\n", mlPort);
	return true;
}

bool cLuxCoopTransport_ENet::StartClient(const tString& asTarget)
{
	if(Init()==false) return false;
	mbHost = false;
	msAddress = asTarget;

	mpHost = enet_host_create(NULL, 1, mlChannelNum, 0, 0);
	if(mpHost==NULL)
	{
		Error("Coop: could not create client host\n");
		return false;
	}
	mfReconnectCount = 0;
	Log("Coop: connecting to %s:%d\n", msAddress.c_str(), mlPort);
	return true;
}

//-----------------------------------------------------------------------

void cLuxCoopTransport_ENet::Stop()
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
	if(mbInitialized)
	{
		enet_deinitialize();
		mbInitialized = false;
	}
}

//-----------------------------------------------------------------------

void cLuxCoopTransport_ENet::Update(float afTimeStep, iLuxCoopTransportListener *apListener)
{
	if(mpHost==NULL) return;

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
			apListener->OnTransportConnected();
			break;

		case ENET_EVENT_TYPE_RECEIVE:
			if(event.peer == mpPeer) apListener->OnTransportPacket(event.packet->data, event.packet->dataLength);
			enet_packet_destroy(event.packet);
			break;

		case ENET_EVENT_TYPE_DISCONNECT:
			if(event.peer == mpPeer)
			{
				mpPeer = NULL;
				mfReconnectCount = kENetReconnectTime;
				apListener->OnTransportDisconnected();
			}
			break;

		default:
			break;
		}
	}

	//Client keeps trying to reach the host until it answers
	if(mbHost==false && mpPeer==NULL && mpHost)
	{
		mfReconnectCount -= afTimeStep;
		if(mfReconnectCount <= 0)
		{
			mfReconnectCount = kENetReconnectTime;
			ENetAddress address;
			enet_address_set_host(&address, msAddress.c_str());
			address.port = (enet_uint16)mlPort;
			mpPeer = enet_host_connect(mpHost, &address, mlChannelNum, 0);
			if(mpPeer==NULL) Error("Coop: could not create connection to %s:%d\n", msAddress.c_str(), mlPort);
		}
	}
}

void cLuxCoopTransport_ENet::Flush()
{
	if(mpHost) enet_host_flush(mpHost);
}

//-----------------------------------------------------------------------

void cLuxCoopTransport_ENet::Send(const unsigned char *apData, size_t alSize, int alChannel, bool abReliable)
{
	if(mpPeer==NULL || alSize==0) return;

	ENetPacket *pPacket = enet_packet_create(apData, alSize, abReliable ? ENET_PACKET_FLAG_RELIABLE : 0);
	enet_peer_send(mpPeer, (enet_uint8)alChannel, pPacket);
}

void cLuxCoopTransport_ENet::DisconnectPeer()
{
	if(mpPeer) enet_peer_disconnect(mpPeer, 0);
}

//-----------------------------------------------------------------------

tString cLuxCoopTransport_ENet::GetStatus()
{
	if(mbHost)
		return "Hosting on port " + cString::ToString(mlPort) + ". Your partner needs your IP (LAN, Radmin VPN or ZeroTier). Allow Amnesia through the Windows firewall.";
	return "Connecting to " + msAddress + ":" + cString::ToString(mlPort) + "...";
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

bool cLuxCoopTransport_ENet::Init()
{
	Stop();
	if(enet_initialize() != 0)
	{
		Error("Coop: could not initialize ENet\n");
		return false;
	}
	mbInitialized = true;
	return true;
}

//-----------------------------------------------------------------------
