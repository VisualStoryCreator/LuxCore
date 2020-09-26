#pragma once

#include <thread>
#include <mutex>
#include <vector>
#include <string>

using namespace std;

/* Service provides communication between this app and client */
class ConnectionService
{
protected:
	enum States
	{
		/* Does nothing. Initial state. You may call StartListening from this state */
		State_None,

		/* Listening for new client (client has not connected yet) */
		State_WaitingForClientConnection,

		/* Communicating with client */
		State_Connected,

		/* Special state used for termination process. State_None is set after successfull termination */
		State_Terminated
	};

public:
	virtual ~ConnectionService();

   /* Start listening
	* connectionId - port, pipe name, etc
	* Returns error code (0 = OK)
	*/
	int StartListening(const string& connectionId);

	/* Stop listening and disconnect */
	virtual void StopListening();

	/* Copy all received data to outReceivedData. Returns false if there is no data received from client (thread safe) */
	virtual bool GetReceivedData(vector<string>& outReceivedData);

	/* Add data to receive buffer (client sends data) (thread safe) */
	virtual void AddReceivedData(const string& data);

	/* Send data to client (thread safe) */
	virtual void Send(const string& data);

	/* Send image data to client (thread safe) */
	virtual void Send(const char* buffer, int offset, int length);

	/* Do some actions when render session has done */
	virtual void NotifyRenderSessionComplete() { }

protected:
	/* Buffer size for rendered image */
	const int IMAGE_BUFFER_SIZE = 1920 * 1080 * 4 * 4 * 2;

	/* Mutex for state change (changing state must be thread-safe) */
	mutex* MutexState = NULL;

	/* Mutex for syncing receive data comes from client */
	mutex* MutexInbox = NULL;

	/* Mutex for syncing data comes to client (rendered image too) */
	mutex* MutexOutbox = NULL;

	/* Buffer for rendered image data */
	char* ImageBuffer = NULL;

	/* Data length in ImageBuffer. 0 = no data */
	int ImageBufferDataLength = 0;

	/* Get data which it is needed to be sent to client. Returns false if there is no data (thread safe) */
	bool GetSendData(vector<string>& outSendData);

	/* Behavior for State == State_WaitingForClientConnection */
	virtual void StateWaitForClientConnection() = 0;

	/* Behavior for State == State_Connected. Listening for data from client */
	virtual void StateConnectedListening() = 0;

	/* Behavior for State == State_Connected. Sending data to client */
	virtual void StateConnectedSending() = 0;

	/* Called from StartListening
	 * connectionId - port, pipe name, etc, something for connection identification
	 * Returns error code. 0 = OK
	 */
	virtual int InitializeConnection(const string& connectionId) = 0;

	/* Shutdown connection routine */
	virtual void CleanUp();

	/* Current state */
	States GetState() const { return state; }

	/* Change current state (if it is allowed) */
	void SetState(States newState);

	/* Clear all data buffer received from client (thread safe) */
	void ClearReceivedData();

	virtual void OnStateChange(States oldState, States newState) { }

private:
	States state = States::State_None;	
	
	bool hasOutboxData = false;  // bufferOutbox.size() > 0 (for multithreading purposes)

	vector<string> bufferInbox;  // buffer for income data
	vector<string> bufferOutbox; // buffer for outcome data

	thread* threadListener;      // thread for listening for income data
	thread* threadSender;        // thread for sending data to client

	void ThreadListenerProc();   // threadListener body

	void ThreadSenderProc();     // threadSender body

	inline void Sleep(int ms) { this_thread::sleep_for(chrono::milliseconds(ms)); } // thread sleep
};
