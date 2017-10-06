#undef UNICODE

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x501
#include <iostream>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <process.h>
#include <vector>

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27022"


std::vector< std::pair<int, SOCKET> > poolOfSockets;
HANDLE mainThreadMutex;
/*
void readn (int newsockfd, char * buffer, int n) {
    int total = 0;
    int k;
    for (int i = 0; i < n; i++) {
        k = recv(newsockfd, buffer + total, 1, 0);
        printf("[%d] symbol i got: %c\n", i, *(buffer + total));
        //printf("My buffer now: %s\n", buffer);
        total += 1;
    	if (k < 0) {
        	perror("ERROR reading from socket");
        	exit(1);
    	}
    }
}*/

int readn(int newsockfd, char *buffer, int n) {
        int nLeft = n;
        int k;
        while (nLeft > 0) {
            k = recv(newsockfd, buffer, nLeft, 0);
            if (k < 0) {
                perror("ERROR reading from socket");
                return -1;
            } else if (k == 0) break;
            buffer = buffer + k;
            nLeft = nLeft - k;
        }
        return n - nLeft;
}

unsigned int __stdcall threadedFunction(void* pArguments) {


        SOCKET ClientSocket = *(SOCKET*) pArguments;
        std::string endString = "end";
        int readed = 1;
        do {
            char recvbuf[DEFAULT_BUFLEN];
            int recvbuflen = DEFAULT_BUFLEN;
            int iSendResult;
            int iResult;
            char *p = recvbuf;
            ZeroMemory(p, recvbuflen);

            readed = readn(ClientSocket, p, recvbuflen);
            if (readed == -1) {
                shutdown(ClientSocket, 2);
                closesocket(ClientSocket);
                break;
            }
            printf("Bytes read: %d\n", readed);
            printf("Received data: %s\n", recvbuf);
            if (recvbuf == endString) {
                shutdown(ClientSocket, 2);
                closesocket(ClientSocket);
                int deleteSock;
                WaitForSingleObject(mainThreadMutex, INFINITE);
                for(int i = 0; i < poolOfSockets.size(); i++) {
                    if (poolOfSockets[i].second == ClientSocket) {
                        printf("found\n");
                        deleteSock = i;
                        break;
                    }
                }
                poolOfSockets.erase(poolOfSockets.begin() + deleteSock);
                ReleaseMutex(mainThreadMutex);
                break;
            }
        } while (readed > 0);
        _endthreadex( 0 );
        return 0;

}

unsigned int __stdcall acceptThreadFunction(void* pArguments) {
        SOCKET ClientSocket = INVALID_SOCKET;
        SOCKET ListenSocket = *(SOCKET*) pArguments;
        HANDLE myThreadHandlers[255];
        unsigned threadId;
        int connectionsCounter = 0;
        while(true){
            ClientSocket = accept(ListenSocket, NULL, NULL);
            if (ClientSocket == INVALID_SOCKET) {
                 printf("accept failed with error: %d\n", WSAGetLastError());
                 closesocket(ListenSocket);
                 break;
            }
            WaitForSingleObject(mainThreadMutex, INFINITE);
            SOCKET *nClientSocket = new SOCKET;
            *nClientSocket = ClientSocket;
            poolOfSockets.push_back(std::make_pair(connectionsCounter, ClientSocket));
            myThreadHandlers[connectionsCounter] = ((HANDLE)_beginthreadex(NULL, 0, &threadedFunction, (void*) nClientSocket, 0, &threadId));
            printf("new client connected with id: %d\n", connectionsCounter);
            connectionsCounter ++;
            ReleaseMutex(mainThreadMutex);
        }
        printf("closing all connections. Ending all threads\n");
        for (int i = 0; i < poolOfSockets.size(); i ++) {
            shutdown(poolOfSockets[i].second, 2);
            closesocket(poolOfSockets[i].second);
        }
        poolOfSockets.clear();
        WaitForMultipleObjects(connectionsCounter, myThreadHandlers, true, INFINITE);
        for (int i = 0; i < connectionsCounter; i++) {
            CloseHandle(myThreadHandlers[i]);
        }
        _endthreadex( 0 );
        return 0;

}
int main(void)
{
        WSADATA wsaData;
        int iResult;

        SOCKET ListenSocket = INVALID_SOCKET;
        HANDLE acceptThreadHandler;

        struct addrinfo *result = NULL;
        struct addrinfo hints;

        unsigned acceptThreadId;

        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
        if (iResult != 0) {
            printf("WSAStartup failed with error: %d\n", iResult);
            return 1;
        }

        iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
        if ( iResult != 0 ) {
            printf("getaddrinfo failed with error: %d\n", iResult);
            WSACleanup();
            return 1;
        }
            // Create a SOCKET for connecting to server
        ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (ListenSocket == INVALID_SOCKET) {
                printf("socket failed with error: %d\n", WSAGetLastError());
                freeaddrinfo(result);
                WSACleanup();
                return 1;
        }
        iResult = bind( ListenSocket, result->ai_addr, (int)result->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            printf("bind failed with error: %d\n", WSAGetLastError());
            freeaddrinfo(result);
            WSACleanup();
            closesocket(ListenSocket);
            return 1;
        }
        iResult = listen(ListenSocket, SOMAXCONN);
        if (iResult == SOCKET_ERROR) {
            printf("listen failed with error: %d\n", WSAGetLastError());
            WSACleanup();
            closesocket(ListenSocket);
            return 1;
        }
        SOCKET *nListenSocket = new SOCKET;
        *nListenSocket = ListenSocket;
        acceptThreadHandler = (HANDLE)_beginthreadex(NULL, 0, &acceptThreadFunction, (void*) nListenSocket, 0, &acceptThreadId);
        std::string stri = "end";
        std::string client = "close";
        std::string sendi = "send";
        std::string viewClient = "show";
        mainThreadMutex = CreateMutex( NULL, FALSE, NULL);

        while (true) {
            char str[255];
            int num = -1;
            printf("---------------\nChoose an operation:\n 1) end\n 2) close\n 3) send\n 4) show\n---------------\n");
            scanf("%s", str);
            if (str == stri) {
                printf("Closing server \n");
                closesocket(ListenSocket);
                WaitForSingleObject(acceptThreadHandler, INFINITE );
                CloseHandle(acceptThreadHandler);
                break;
            } else {
                if (str == client) {
                    printf("---------------\nWrite an id of a client to disconnect\n---------------\n");
                    scanf("%d", &num);

                    WaitForSingleObject(mainThreadMutex, INFINITE);
                    int indexToDelete;
                    for(int i = 0; i < poolOfSockets.size(); i++) {
                        if (poolOfSockets[i].first == num) {
                            indexToDelete = i;
                            break;
                        }
                    }
                    shutdown(poolOfSockets[indexToDelete].second, 2);
                    closesocket(poolOfSockets[indexToDelete].second);
                    poolOfSockets.erase(poolOfSockets.begin() + indexToDelete);
                    ReleaseMutex(mainThreadMutex);
                } else {
                    if (str == sendi) {
                        printf("---------------\nWrite an id of a client to send a message\n---------------\n");
                        scanf("%d", &num);
                        fflush(stdin);
                        char sendbuf[DEFAULT_BUFLEN];
                        printf("Your message: ");
                        //scanf("%s", sendbuf);
                        fgets(sendbuf, DEFAULT_BUFLEN, stdin);
                        printf("%s\n", sendbuf);
                        WaitForSingleObject(mainThreadMutex, INFINITE);
                        SOCKET sendSock;
                        for(int i = 0; i < poolOfSockets.size(); i++) {
                            if(poolOfSockets[i].first == num) {
                                sendSock = poolOfSockets[i].second;
                            }
                        }

                        int iSendResult ;
                        iSendResult = send(sendSock, sendbuf, DEFAULT_BUFLEN, 0);
                        if (iSendResult == SOCKET_ERROR) {
                            printf("send failed with error: %d\n", WSAGetLastError());
                            printf("deleting socket from pool of sockets \n");
                            shutdown(sendSock, 2);
                            closesocket(sendSock);
                        }
                        ReleaseMutex(mainThreadMutex);
                    } else {
                        if (str == viewClient) {
                            printf("Available clients: ");
                            WaitForSingleObject(mainThreadMutex, INFINITE);
                            for(int i = 0; i < poolOfSockets.size(); i++) {
                                printf("%d, ", poolOfSockets[i].first);
                            }
                            printf("\n");
                            ReleaseMutex(mainThreadMutex);
                        }
                    }
                }
            }
        }
        WSACleanup();
        return 0;
}

