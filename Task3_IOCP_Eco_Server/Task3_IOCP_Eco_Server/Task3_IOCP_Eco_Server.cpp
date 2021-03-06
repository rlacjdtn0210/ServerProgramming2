#include "pch.h"
#include <winsock2.h>
#include <iostream>
#include <tchar.h>
#include <time.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma warning(disable:4996)

#define MAX_BUFFER        1024
#define SERVER_PORT        3500

struct SOCKETINFO
{
	WSAOVERLAPPED overlapped;
	WSABUF dataBuffer;
	SOCKET socket;
	char messageBuffer[MAX_BUFFER];
	int receiveBytes;
	DWORD sendBytes;
};

DWORD WINAPI makeThread(LPVOID hIOCP);

int _tmain(int argc, _TCHAR* argv[])
{
	// Winsock Start - windock.dll 로드
	WSADATA WSAData;
	if (WSAStartup(MAKEWORD(2, 2), &WSAData) != 0)
	{
		printf("Error - Can not load 'winsock.dll' file\n");
		return 1;
	}

	// 소켓생성  
	SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listenSocket == INVALID_SOCKET)
	{
		printf("Error - Invalid socket\n");
		return 1;
	}

	// 서버정보 객체설정
	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = PF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	// 소켓설정
	if (bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
		printf("Error - Fail bind\n");
		// 6. 소켓종료
		closesocket(listenSocket);
		// Winsock End
		WSACleanup();
		return 1;
	}

	// 수신대기열생성
	if (listen(listenSocket, 5) == SOCKET_ERROR)
	{
		printf("Error - Fail listen\n");
		// 6. 소켓종료
		closesocket(listenSocket);
		// Winsock End
		WSACleanup();
		return 1;
	}


	// 완료결과를 처리하는 객체(CP : Completion Port) 생성
	HANDLE hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	// 워커스레드 생성
	// - CPU * 2개
	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);
	int threadCount = systemInfo.dwNumberOfProcessors * 2;
	unsigned long threadId;
	// - thread Handler 선언
	HANDLE *hThread = (HANDLE *)malloc(threadCount * sizeof(HANDLE));
	// - thread 생성
	for (int i = 0; i < threadCount; i++)
	{
		hThread[i] = CreateThread(NULL, 0, makeThread, &hIOCP, 0, &threadId);
	}

	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(SOCKADDR_IN);
	memset(&clientAddr, 0, addrLen);
	SOCKET clientSocket;
	SOCKETINFO *socketInfo;
	DWORD receiveBytes;
	DWORD flags;

	while (1)
	{
		clientSocket = accept(listenSocket, (struct sockaddr *)&clientAddr, &addrLen);
		if (clientSocket == INVALID_SOCKET)
		{
			printf("Error - Accept Failure\n");
			return 1;
		}

		time_t current_time;
		tm *struct_time;
		time(&current_time);
		struct_time = localtime(&current_time);
		char messageBuffer[MAX_BUFFER];

		for (int i = 0; i < MAX_BUFFER; i++) messageBuffer[i] = '\0';
		itoa(struct_time->tm_year + 1900, messageBuffer, 10);
		messageBuffer[4] = 'Y';
		messageBuffer[5] = ' ';
		itoa(struct_time->tm_mon + 1, messageBuffer + 6, 10);
		messageBuffer[8] = 'M';
		messageBuffer[9] = ' ';
		itoa(struct_time->tm_mday, messageBuffer + 10, 10);
		messageBuffer[12] = 'D';
		messageBuffer[13] = ' ';
		itoa(struct_time->tm_hour, messageBuffer + 14, 10);
		messageBuffer[16] = 'h';
		messageBuffer[17] = ' ';
		itoa(struct_time->tm_min, messageBuffer + 18, 10);
		messageBuffer[20] = 'm';
		messageBuffer[21] = ' ';
		itoa(struct_time->tm_sec, messageBuffer + 22, 10);
		messageBuffer[24] = 's';

		socketInfo = (struct SOCKETINFO *)malloc(sizeof(struct SOCKETINFO));
		memset((void *)socketInfo, 0x00, sizeof(struct SOCKETINFO));
		socketInfo->socket = clientSocket;
		socketInfo->receiveBytes = 0;
		socketInfo->sendBytes = 0;
		socketInfo->dataBuffer.len = MAX_BUFFER;
		socketInfo->dataBuffer.buf = messageBuffer;
		flags = 0;

		if (WSASend(socketInfo->socket, &(socketInfo->dataBuffer), 1, &(socketInfo->sendBytes), 0, NULL, NULL) == SOCKET_ERROR)
		{
			if (WSAGetLastError() != WSA_IO_PENDING)
			{
				printf("Error - Fail WSASend(error_code : %d)\n", WSAGetLastError());
			}
		}

		printf("TRACE - Send message : %s\n", socketInfo->dataBuffer.buf);

		hIOCP = CreateIoCompletionPort((HANDLE)clientSocket, hIOCP, (DWORD)socketInfo, 0);

		// 중첩 소캣을 지정하고 완료시 실행될 함수를 넘겨준다.
		if (WSARecv(socketInfo->socket, &socketInfo->dataBuffer, 1, &receiveBytes, &flags, &(socketInfo->overlapped), NULL))
		{
			if (WSAGetLastError() != WSA_IO_PENDING)
			{
				printf("Error - IO pending Failure\n");
				return 1;
			}
		}
	}

	// 리슨 소켓종료
	closesocket(listenSocket);

	// Winsock End
	WSACleanup();

	return 0;
}

DWORD WINAPI makeThread(LPVOID hIOCP)
{
	HANDLE threadHandler = *((HANDLE *)hIOCP);
	DWORD receiveBytes;
	DWORD sendBytes;
	DWORD completionKey;
	DWORD flags;
	struct SOCKETINFO *eventSocket;
	while (1)
	{
		// 입출력 완료 대기
		if (GetQueuedCompletionStatus(threadHandler, &receiveBytes, &completionKey, (LPOVERLAPPED *)&eventSocket, INFINITE) == 0)
		{
			printf("Error - GetQueuedCompletionStatus Failure\n");
			closesocket(eventSocket->socket);
			free(eventSocket);
			return 1;
		}

		eventSocket->dataBuffer.len = receiveBytes;

		if (receiveBytes == 0)
		{
			closesocket(eventSocket->socket);
			free(eventSocket);
			continue;
		}
		else
		{

			if (WSASend(eventSocket->socket, &(eventSocket->dataBuffer), 1, &sendBytes, 0, NULL, NULL) == SOCKET_ERROR)
			{
				if (WSAGetLastError() != WSA_IO_PENDING)
				{
					printf("Error - Fail WSASend(error_code : %d)\n", WSAGetLastError());
				}
			}

			//printf("TRACE - Send message : %s\n", eventSocket->dataBuffer.buf);

			memset(eventSocket->messageBuffer, 0x00, MAX_BUFFER);
			eventSocket->receiveBytes = 0;
			eventSocket->sendBytes = 0;
			eventSocket->dataBuffer.len = MAX_BUFFER;
			eventSocket->dataBuffer.buf = eventSocket->messageBuffer;
			flags = 0;

			if (WSARecv(eventSocket->socket, &(eventSocket->dataBuffer), 1, &receiveBytes, &flags, &eventSocket->overlapped, NULL) == SOCKET_ERROR)
			{
				if (WSAGetLastError() != WSA_IO_PENDING)
				{
					printf("Error - Fail WSARecv(error_code : %d)\n", WSAGetLastError());
				}
			}
		}
	}
}