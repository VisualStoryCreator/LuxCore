#pragma once

#include "ConnectionService.h"

class StandaloneListener : public ConnectionService
{
protected:
	virtual void StateWaitForClientConnection() override;

	virtual void StateConnectedListening() override;

	virtual void StateConnectedSending() override;

	virtual int InitializeConnection(const string& connectionId) override;
};
