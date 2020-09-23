#pragma once

#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include "ConnectionService.h"

using namespace std;

/* Service provides communication between this app and client via TCP protocol */
class TCPListener : public ConnectionService
{
public:

protected:
	virtual void StateWaitForClientConnection() override;

	virtual void StateConnectedListening() override;

	virtual void StateConnectedSending() override;

	virtual int InitializeConnection(const string& connectionId) override;

	virtual void CleanUp() override;

private:
	u_short listeningPort;
	WSAData wsaData;

	SOCKET listenerSocket = NULL;
	SOCKET clientSocket = NULL;
};
