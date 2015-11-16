#include <stdio.h>   
#include <string.h>  
#include <sys/socket.h>  
#include <arpa/inet.h>   
#include "helper.h"

int main(int argc , char *argv[])
{
    int sock;
    struct sockaddr_in server;
    char message[1000] , server_reply[2000];

    //Create socket
    sock = socket(AF_INET , SOCK_STREAM , 0);
    if (sock == -1)
    {
        printf("cound'n create socket");
    }
    puts("Socket created");

    server.sin_addr.s_addr = inet_addr(SERV_IP);
    server.sin_family = AF_INET;
    server.sin_port = htons(SERV_PORT);

    //Connect to remote server
    if (connect(sock , (struct sockaddr *)&server , sizeof(server)) < 0)
    {
        printf("connection error");
        return 1;
    }

    puts("Connected\n");

    //keep communicating with server
    while(1)
    {
        printf("write your message : ");
        gets(message);

        //Send some data
        if( send(sock , message , strlen(message) , 0) < 0)
        {
            puts("send error");
            return 1;
        }

        //Receive a reply from the server
        bzero(server_reply,2000);
        if( recv(sock , server_reply , 2000 , 0) < 0)
        {
            puts("recv failed");
            break;
        }

        puts("Server reply :");
        puts(server_reply);

        break;
    }

    sleep(10);
    close(sock);
    return 0;
}