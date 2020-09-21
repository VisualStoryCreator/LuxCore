#pragma once

#include <thread>
#include <mutex>
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>

using namespace std;

class TCPListener
{
public:
	int StartListening(const u_short listeningPort);

	void StopListening();

	bool GetReceivedData(vector<string>& OutReceivedData);

	void AddReceivedData(const string& Data);

	void Send(const string& Data);

	void Send(const char* buffer, int offset, int length);

private:
	mutex* receiveDataMutex = NULL;
	mutex* sendDataMutex = NULL;
	thread* listeningThread = NULL;
	thread* sendingThread = NULL;

	bool bIsTerminated = true;
	u_short listeningPort = 0;
	
	WSAData wsaData;
	SOCKET listenerSocket = NULL;
	SOCKET clientSocket = NULL;

	bool hasSendData = false;
	vector<string> receivedData;
	vector<string> sendData;

	const int IMAGE_BUFFER_SIZE = 1920 * 1080 * 4 * 4 * 2;
	char* imageBuffer = NULL;
	int imageBufferDataLength = 0;

	int CreateListenerSocket(u_short listeningPort);

	void ListeningThreadProc();

	void SendThreadProc();

	bool GetSendData(vector<string>& OutSendData);

	inline void Sleep(int ms) { this_thread::sleep_for(chrono::milliseconds(ms)); }
};
