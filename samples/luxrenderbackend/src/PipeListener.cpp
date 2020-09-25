#include "PipeListener.h"

void PipeListener::StateWaitForClientConnection()
{
	if (printPipeNameToConsole)
	{
		printf("Listening on pipe ");
		printf(pipeName.c_str());
		printf("\n");

		// we are in overlapping mode, so do not spam,
		printPipeNameToConsole = false;
	}

	OVERLAPPED overlapped;
	ZeroMemory(&overlapped, sizeof(overlapped));

	ConnectNamedPipe(pipe, &overlapped);
	WaitForSingleObject(pipe, 500);

	DWORD tmp;
	bool isConnected = GetOverlappedResult(pipe, &overlapped, &tmp, false);
	if (isConnected)
	{
		printf("Client connected\n");
		SetState(States::State_Connected);
		printPipeNameToConsole = true;
	}
}

void PipeListener::StateConnectedListening()
{
	OVERLAPPED overlapped;
	ZeroMemory(&overlapped, sizeof(overlapped));

	ReadFile(pipe, bufferTempInbox, PIPE_INBOX_BUFFER_SIZE, NULL, &overlapped);

	DWORD bytesRead;
	WaitForSingleObject(pipe, 100);
	bool success = GetOverlappedResult(pipe, &overlapped, &bytesRead, false);

	if (success)
	{
		// data received
		string dataStr = string(bufferTempInbox, bytesRead);

		printf("received: ");
		printf(dataStr.c_str());
		printf("\n");

		AddReceivedData(dataStr);
	}
	else
	{
		if (!FixIOError())
		{
			SetState(States::State_WaitingForClientConnection);
		}
	}
}

void PipeListener::StateConnectedSending()
{
	OVERLAPPED overlapped;
	ZeroMemory(&overlapped, sizeof(overlapped));

	// send strings
	vector<string> bufferOutbox;
	if (GetSendData(bufferOutbox)) // has data to send?
	{
		for (string s : bufferOutbox)
		{
			DWORD bytesWritten;
			WriteFile(pipe, s.c_str(), s.length(), NULL, &overlapped);
			WaitForSingleObject(pipe, 100);

			bool result = GetOverlappedResult(pipe, &overlapped, &bytesWritten, false);
			if (result)
			{
				if (s.length() != bytesWritten)
				{
					printf("Not all string data was sent to client");
				}
			}
			else
			{
				if (FixIOError())
				{
					SetState(States::State_WaitingForClientConnection);
					return;
				}
			}
		}
	}

	// send image data
	MutexOutbox->lock();

	if (ImageBufferDataLength > 0)
	{
		ZeroMemory(&overlapped, sizeof(overlapped));

		DWORD bytesWritten;
		WriteFile(pipe, ImageBuffer, ImageBufferDataLength, 0, &overlapped);
		WaitForSingleObject(pipe, 100);
		bool result = GetOverlappedResult(pipe, &overlapped, &bytesWritten, false);

		if (result)
		{
			if (ImageBufferDataLength != bytesWritten)
			{
				printf("Not all string data was sent to client");
			}

			ImageBufferDataLength = 0;
		}
		else
		{
			if (!FixIOError())
			{
				SetState(States::State_WaitingForClientConnection);
			}
		}
	}

	MutexOutbox->unlock();
}

int PipeListener::InitializeConnection(const string& connectionId)
{
	pipeName = connectionId;

	wstring stemp = wstring(connectionId.begin(), connectionId.end());
	LPCWSTR pipeNameW = stemp.c_str();

	pipe = CreateNamedPipe(
		pipeNameW,			      // pipe name 
		PIPE_ACCESS_DUPLEX |      // read/write access
		FILE_FLAG_OVERLAPPED,     // overlapped mode (async)
		PIPE_TYPE_BYTE |          // pipe type
		PIPE_READMODE_BYTE |      // read mode
		PIPE_WAIT,                // blocking mode 
		PIPE_UNLIMITED_INSTANCES, // max. instances  
		PIPE_OUTBOX_BUFFER_SIZE,  // output buffer size 
		PIPE_INBOX_BUFFER_SIZE,   // input buffer size 
		0,                        // client time-out 
		NULL);                    // default security attribute

	if (pipe == INVALID_HANDLE_VALUE)
	{
		pipe = NULL;
		return GetLastError();
	}

	bufferTempInbox = (char*)malloc(PIPE_INBOX_BUFFER_SIZE);
	bufferTempOutbox = (char*)malloc(PIPE_OUTBOX_BUFFER_SIZE);

	return 0; // success
}

void PipeListener::CleanUp()
{
	__super::CleanUp();

	if (pipe)
	{
		FlushFileBuffers(pipe);
		DisconnectNamedPipe(pipe);
		CloseHandle(pipe);
		pipe = NULL;
	}

	if (bufferTempInbox)
	{
		free(bufferTempInbox);
		bufferTempInbox = NULL;
	}

	if (bufferTempOutbox)
	{
		free(bufferTempOutbox);
		bufferTempOutbox = NULL;
	}
}

bool PipeListener::FixIOError()
{
	int error = GetLastError();

	switch (error)
	{
	case ERROR_IO_INCOMPLETE:
		return true;
	case ERROR_BROKEN_PIPE:
		printf("Client disconnected\n");
		return false;
	default:
		printf("Pipe error %d\n", error);
		return false;
	}
}
