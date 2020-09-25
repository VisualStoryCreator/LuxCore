#pragma once

#include <windows.h> 
#include <stdio.h> 
#include <tchar.h>
#include <strsafe.h>
#include "ConnectionService.h"

class PipeListener : public ConnectionService
{
protected:
	virtual void StateWaitForClientConnection() override;

	virtual void StateConnectedListening() override;

	virtual void StateConnectedSending() override;

	virtual int InitializeConnection(const string& connectionId) override;

	virtual void CleanUp() override;

private:
	string pipeName;

	bool printPipeNameToConsole = true;

	const int PIPE_INBOX_BUFFER_SIZE = 512;
	const int PIPE_OUTBOX_BUFFER_SIZE = 512;

	HANDLE pipe;

	char* bufferTempInbox = NULL;
	char* bufferTempOutbox = NULL;

	bool FixIOError();
};
