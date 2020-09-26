#include "ConnectionService.h"

ConnectionService::~ConnectionService()
{
	CleanUp();
}

int ConnectionService::StartListening(const string& connectionId)
{
	if (GetState() != States::State_None)
	{
		throw new runtime_error("already started");
	}

	int result = InitializeConnection(connectionId);

	if (result == 0) // success?
	{
		ImageBuffer = (char*)malloc(IMAGE_BUFFER_SIZE);

		MutexState = new mutex();
		MutexOutbox = new mutex();
		MutexInbox = new mutex();

		threadListener = new thread(&ConnectionService::ThreadListenerProc, this);
		threadSender = new thread(&ConnectionService::ThreadSenderProc, this);

		SetState(States::State_WaitingForClientConnection);
	}

	return result;
}

void ConnectionService::StopListening()
{
	CleanUp();
}

bool ConnectionService::GetReceivedData(vector<string>& outReceivedData)
{
	MutexInbox->lock();

	bool hasReceivedData = bufferInbox.size() > 0;
	if (hasReceivedData)
	{
		for (string s : bufferInbox)
		{
			outReceivedData.push_back(s);
		}
		bufferInbox.clear();
	}

	MutexInbox->unlock();

	return hasReceivedData;
}

bool ConnectionService::GetSendData(vector<string>& outSendData)
{
	if (!hasOutboxData)
	{
		return false;
	}

	MutexOutbox->lock();

	for (string s : bufferOutbox)
	{
		outSendData.push_back(s);
	}

	bufferOutbox.clear();
	hasOutboxData = false;

	MutexOutbox->unlock();

	return true;
}

void ConnectionService::CleanUp()
{
	if (GetState() == States::State_None)
	{
		return;
	}

	SetState(States::State_Terminated);

	MutexOutbox->lock();
	if (ImageBuffer)
	{
		free(ImageBuffer);
		ImageBuffer = NULL;
	}
	MutexOutbox->unlock();

	if (threadListener)
	{
		threadListener->join();
		delete threadListener;
		threadListener = NULL;
	}

	if (threadSender)
	{
		threadSender->join();
		delete threadSender;
		threadSender = NULL;
	}

	if (MutexOutbox)
	{
		delete MutexOutbox;
		MutexOutbox = NULL;
	}

	if (MutexInbox)
	{
		delete MutexInbox;
		MutexInbox = NULL;
	}

	if (MutexState)
	{
		delete MutexState;
		MutexState = NULL;
	}

	state = States::State_None;
}

void ConnectionService::SetState(States newState)
{
	MutexState->lock();
	if (state != newState && state != States::State_Terminated)
	{
		States oldState = state;
		state = newState;
		OnStateChange(oldState, newState);
	}
	MutexState->unlock();
}

void ConnectionService::ClearReceivedData()
{
	MutexInbox->lock();
	bufferInbox.clear();
	MutexInbox->unlock();
}

void ConnectionService::AddReceivedData(const string& data)
{
	if (GetState() == States::State_Terminated || data.empty())
	{
		return;
	}

	if (MutexInbox)
	{
		MutexInbox->lock();
		bufferInbox.push_back(data);
		MutexInbox->unlock();
	}
	else
	{
		bufferInbox.push_back(data);
	}
}

void ConnectionService::Send(const string& data)
{
	if (GetState() == States::State_Terminated || GetState() == States::State_None || data.empty())
	{
		return;
	}

	MutexOutbox->lock();

	bufferOutbox.push_back(data);
	hasOutboxData = true;
	
	MutexOutbox->unlock();
}

void ConnectionService::Send(const char* buffer, int offset, int length)
{
	if (GetState() == States::State_Terminated || GetState() == States::State_None || length == 0)
	{
		return;
	}

	if (length > IMAGE_BUFFER_SIZE)
	{
		printf("Image data size exceeds buffer length\n");
		return;
	}

	MutexOutbox->lock();

	memcpy(ImageBuffer, buffer + offset, length);
	ImageBufferDataLength = length;

	MutexOutbox->unlock();
}

void ConnectionService::ThreadListenerProc()
{
	while (GetState() != States::State_Terminated)
	{
		if (GetState() == States::State_WaitingForClientConnection)
		{
			StateWaitForClientConnection();
		}
		else if (GetState() == States::State_Connected)
		{
			StateConnectedListening();
		}

		Sleep(10);
	}
}

void ConnectionService::ThreadSenderProc()
{
	while (GetState() != States::State_Terminated)
	{
		if (GetState() == States::State_Connected)
		{
			StateConnectedSending();
		}

		Sleep(10);
	}
}
