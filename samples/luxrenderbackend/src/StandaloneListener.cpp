#include "StandaloneListener.h"

void StandaloneListener::StateWaitForClientConnection()
{
	printf("Standalone mode\n");

	SetState(States::State_Connected);
}

void StandaloneListener::StateConnectedListening() { }

void StandaloneListener::StateConnectedSending() { }

int StandaloneListener::InitializeConnection(const string& connectionId)
{
	AddReceivedData("render " + connectionId);
	return 0;
}
