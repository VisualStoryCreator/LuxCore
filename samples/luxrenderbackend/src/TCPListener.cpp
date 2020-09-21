#include "TCPListener.h"

int TCPListener::StartListening(const u_short listeningPort)
{
	// stop prev listening
	if (listeningThread)
	{
		throw new runtime_error("Already listening");
	}

	imageBuffer = (char*)malloc(IMAGE_BUFFER_SIZE);

	bIsTerminated = false;

	int result = CreateListenerSocket(listeningPort);
	if (result == 0)
	{
		receiveDataMutex = new mutex();
		listeningThread = new thread(&TCPListener::ListeningThreadProc, this);

		sendDataMutex = new mutex();
		sendingThread = new thread(&TCPListener::SendThreadProc, this);
	}

	return result;
}

void TCPListener::StopListening()
{
	bIsTerminated = true;

	if (clientSocket != 0)
	{
		closesocket(clientSocket);
	}

	if (listenerSocket != 0)
	{
		closesocket(listenerSocket);
	}

	WSACleanup();

	sendingThread->join();
	delete sendingThread;
	sendingThread = NULL;

	listeningThread->join();
	delete listeningThread;
	listeningThread = NULL;

	delete sendDataMutex;
	sendDataMutex = NULL;

	delete receiveDataMutex;
	receiveDataMutex = NULL;

	if (imageBuffer)
	{
		free(imageBuffer);
		imageBuffer = NULL;
		imageBufferDataLength = 0;
	}
}

int TCPListener::CreateListenerSocket(u_short listeningPort)
{
	int result;

	this->listeningPort = listeningPort;

	// WSA
	ZeroMemory(&wsaData, sizeof(wsaData));
	result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0)
	{
		return WSAGetLastError();
	}

	// Address info

	struct addrinfo addressInfo;
	ZeroMemory(&addressInfo, sizeof(addressInfo));
	
	addressInfo.ai_family = AF_INET;
	addressInfo.ai_socktype = SOCK_STREAM;
	addressInfo.ai_protocol = IPPROTO_TCP;
	addressInfo.ai_flags = AI_PASSIVE;

	// Resolve the server address and port
	struct addrinfo *resolvedAddressInfo = NULL;
	string port = to_string(listeningPort);
	result = getaddrinfo(NULL, port.c_str(), &addressInfo, &resolvedAddressInfo);
	if (result != 0) {
		int errorCode = WSAGetLastError();
		WSACleanup();
		errorCode;
	}

	// Create listening socket
	listenerSocket = socket(resolvedAddressInfo->ai_family, resolvedAddressInfo->ai_socktype, resolvedAddressInfo->ai_protocol);
	if (listenerSocket == INVALID_SOCKET) {
		int errorCode = WSAGetLastError();
		freeaddrinfo(resolvedAddressInfo);
		WSACleanup();
		return errorCode;
	}
	
	// Setup the TCP listening socket
	result = ::bind(listenerSocket, resolvedAddressInfo->ai_addr, (int)resolvedAddressInfo->ai_addrlen);
	if (result == SOCKET_ERROR) {
		int errorCode = WSAGetLastError();
		freeaddrinfo(resolvedAddressInfo);
		closesocket(listenerSocket);
		WSACleanup();
		return errorCode;
	}

	freeaddrinfo(resolvedAddressInfo);
	return 0;
}

void TCPListener::ListeningThreadProc()
{
	while (!bIsTerminated)
	{
		if (!clientSocket)
		{
			// listen for new connection
			printf("Listening on port %d\n", listeningPort);
			int result = listen(listenerSocket, SOMAXCONN);
			if (result == SOCKET_ERROR) {
				int errorCode = WSAGetLastError();
				if (errorCode == WSAEINTR)
				{
					// iterrupted by user (may be exit command)
					break;
				}
				else
				{
					printf("Listen failed with error: %d\n", errorCode);
					closesocket(listenerSocket);
					clientSocket = 0;
					break;
				}
			}

			// accept connection
			clientSocket = accept(listenerSocket, NULL, NULL);
			if (clientSocket == INVALID_SOCKET) {
				int errorCode = WSAGetLastError();
				if (errorCode == WSAEINTR)
				{
					// iterrupted by user (may be exit command)
					break;
				}
				else
				{
					printf("Accept failed with error: %d\n", errorCode);
					closesocket(listenerSocket);
					clientSocket = 0;
					break;
				}
			}

			printf("Client accepted\n");
		}
		else 
		{
			const int BUFFER_LENGTH = 512;
			char receiveBuffer[BUFFER_LENGTH];

			receiveDataMutex->lock();
			receivedData.clear();
			receiveDataMutex->unlock();

			// listen for data
			while (true)
			{
				int bytesReceived = recv(clientSocket, receiveBuffer, BUFFER_LENGTH, 0);
				if (bytesReceived == 0)
				{
					// client disconnected
					closesocket(clientSocket);
					clientSocket = NULL;
					printf("Client disconnected\n");
					break;
				}
				else if (bytesReceived == SOCKET_ERROR)
				{
					// error
					int errorCode = WSAGetLastError();
					if (errorCode == WSAEINTR)
					{
						// iterrupted by user (may be exit command)
						break;
					}
					else
					{
						printf("Connection aborted with error: %d\n", errorCode);
						closesocket(listenerSocket);
						clientSocket = 0;
						break;
					}
				}
				else
				{
					// data received successfully
					receiveDataMutex->lock();
					receivedData.push_back(string(receiveBuffer, bytesReceived));
					receiveDataMutex->unlock();
				}
			}
		}

		Sleep(10);
	}

	printf("Listening stopped\n");
}

void TCPListener::SendThreadProc()
{
	vector<string> sendData;
	while (!bIsTerminated)
	{
		if (clientSocket && clientSocket != SOCKET_ERROR)
		{
			sendData.clear();
			if (GetSendData(sendData))
			{
				// has data to send
				for (string s : sendData)
				{
					int errorCode = send(clientSocket, s.c_str(), s.length(), 0);
					if (errorCode == SOCKET_ERROR)
					{
						if (errorCode != WSAEINTR)
						{
							printf("Send failed with error: %d\n", WSAGetLastError());
						}
					}
				}
			}

			sendDataMutex->lock();
			if (imageBufferDataLength > 0)
			{
				int errorCode = send(clientSocket, imageBuffer, imageBufferDataLength, 0);
				if (errorCode == SOCKET_ERROR)
				{
					if (errorCode != WSAEINTR)
					{
						printf("Send failed with error: %d\n", WSAGetLastError());
					}
				}
				else
				{
					printf("Rendered image data has been sent to client\n");
				}

				imageBufferDataLength = 0;
			}
			sendDataMutex->unlock();

			Sleep(20);
		}
		else
		{
			Sleep(500);
		}
	}
}

bool TCPListener::GetSendData(vector<string>& OutSendData)
{
	if (!hasSendData)
	{
		return false;
	}

	sendDataMutex->lock();

	for (string s : sendData)
	{
		OutSendData.push_back(s);
	}

	sendData.clear();
	hasSendData = false;

	sendDataMutex->unlock();

	return true;
}

bool TCPListener::GetReceivedData(vector<string>& OutReceivedData)
{
	if (receiveDataMutex)
	{
		receiveDataMutex->lock();

		bool hasReceivedData = receivedData.size() > 0;
		if (hasReceivedData)
		{
			for (string s : receivedData)
			{
				OutReceivedData.push_back(s);
			}
			receivedData.clear();
		}

		receiveDataMutex->unlock();

		return hasReceivedData;
	}
	else
	{
		bool hasReceivedData = receivedData.size() > 0;
		if (hasReceivedData)
		{
			for (string s : receivedData)
			{
				OutReceivedData.push_back(s);
			}
			receivedData.clear();
		}

		return hasReceivedData;
	}
}

void TCPListener::AddReceivedData(const string& Data)
{
	if (Data.empty())
	{
		return;
	}

	if (receiveDataMutex)
	{
		receiveDataMutex->lock();
		receivedData.push_back(Data);
		receiveDataMutex->unlock();
	}
	else
	{
		receivedData.push_back(Data);
	}
}

void TCPListener::Send(const string& Data)
{
	if (Data.empty())
	{
		return;
	}

	if (sendDataMutex)
	{
		sendDataMutex->lock();
		sendData.push_back(Data);
		hasSendData = true;
		sendDataMutex->unlock();
	}
	else
	{
		sendData.push_back(Data);
	}
}

void TCPListener::Send(const char* buffer, int offset, int length)
{
	sendDataMutex->lock();
	memcpy(imageBuffer, buffer + offset, length);
	imageBufferDataLength = length;
	sendDataMutex->unlock();
}
