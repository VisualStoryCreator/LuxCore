#include "TCPListener.h"

void TCPListener::StateWaitForClientConnection()
{
	 // listen for new connection
	printf("Listening on port %d\n", listeningPort);
	int result = listen(listenerSocket, SOMAXCONN);
	if (result == SOCKET_ERROR)
	{
		int errorCode = WSAGetLastError();
		if (errorCode == WSAEINTR)
		{
			// iterrupted by user (may be exit command)
			SetState(States::State_WaitingForClientConnection);
			return;
		}
		else
		{
			printf("Listen failed with error: %d\n", errorCode);
			closesocket(listenerSocket);
			clientSocket = NULL;
			SetState(States::State_WaitingForClientConnection);
			return;
		}
	}

	// accept connection
	clientSocket = accept(listenerSocket, NULL, NULL);
	if (clientSocket == INVALID_SOCKET)
	{
		int errorCode = WSAGetLastError();
		if (errorCode == WSAEINTR)
		{
			// iterrupted by user (may be exit command)
			clientSocket = NULL;
			SetState(States::State_WaitingForClientConnection);
			return;
		}
		else
		{
			printf("Accept failed with error: %d\n", errorCode);
			closesocket(clientSocket);
			clientSocket = NULL;
			SetState(States::State_WaitingForClientConnection);
			return;
		}
	}

	printf("Client accepted\n");
	SetState(States::State_Connected);
}

void TCPListener::StateConnectedListening()
{
	const int BUFFER_LENGTH = 512;
	char receiveBuffer[BUFFER_LENGTH];

	// listen for data
	int bytesReceived = recv(clientSocket, receiveBuffer, BUFFER_LENGTH, 0);
	if (bytesReceived == 0)
	{
		// client disconnected
		closesocket(clientSocket);
		clientSocket = NULL;
		SetState(States::State_WaitingForClientConnection);
		printf("Client disconnected\n");
		return;
	}
	else if (bytesReceived == SOCKET_ERROR)
	{
		// error
		int errorCode = WSAGetLastError();
		if (errorCode == WSAEINTR)
		{
			// iterrupted by user (may be exit command)
			closesocket(clientSocket);
			clientSocket = NULL;
			SetState(States::State_WaitingForClientConnection);
			return;
		}
		else
		{
			printf("Connection aborted with error: %d\n", errorCode);
			closesocket(clientSocket);
			clientSocket = NULL;
			SetState(States::State_WaitingForClientConnection);
			return;
		}
	}
	else
	{
		// data received successfully
		AddReceivedData(string(receiveBuffer, bytesReceived));
	}
}

void TCPListener::StateConnectedSending()
{
	// send strings
	vector<string> bufferOutbox;
	if (GetSendData(bufferOutbox))
	{
		// has data to send
		for (string s : bufferOutbox)
		{
			int errorCode = send(clientSocket, s.c_str(), s.length(), 0);
			if (errorCode == SOCKET_ERROR)
			{
				if (errorCode != WSAEINTR)
				{
					printf("Send failed with error: %d\n", WSAGetLastError());
					closesocket(clientSocket);
					clientSocket = NULL;
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
		int errorCode = send(clientSocket, ImageBuffer, ImageBufferDataLength, 0);
		if (errorCode == SOCKET_ERROR)
		{
			if (errorCode != WSAEINTR)
			{
				printf("Send failed with error: %d\n", WSAGetLastError());
				closesocket(clientSocket);
				clientSocket = NULL;
				SetState(States::State_Connected);
			}
		}
		else
		{
			printf("Rendered image data has been sent to client\n");
		}
		
		ImageBufferDataLength = 0;
	}
	MutexOutbox->unlock();
}

int TCPListener::InitializeConnection(const string& connectionId)
{
	int result;
	listeningPort = atoi(connectionId.c_str());

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
	string port = connectionId;
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

void TCPListener::CleanUp()
{
	if (clientSocket != NULL)
	{
		closesocket(clientSocket);
		clientSocket = NULL;
	}

	if (listenerSocket != NULL)
	{
		closesocket(listenerSocket);
		listenerSocket = NULL;
	}

	WSACleanup();
	__super::CleanUp();
}
