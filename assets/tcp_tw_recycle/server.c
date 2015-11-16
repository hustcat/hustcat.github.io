//server.c
#include "helper.h"
#include <sys/epoll.h>
#include <fcntl.h>

#define EVENT_ARR_SIZE 20
#define EPOLL_SIZE     20

void setnonblocking(
    int sockfd
);

int
main(int argc, char **argv)
{
    int        i,  listenfd, connfd, sockfd, epfd;
    ssize_t        n;
    char            buf[MAXLINE];
    socklen_t        clilen;
    struct sockaddr_in    cliaddr, servaddr;
    struct epoll_event ev, evs[EVENT_ARR_SIZE];
    int   nfds;

    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_sys("create socket error!\n");
    setnonblocking(listenfd);

    epfd = epoll_create(EPOLL_SIZE);
    ev.data.fd = listenfd;
    ev.events = EPOLLIN | EPOLLET;
    if(epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev) < 0)
        err_sys("epoll_ctl listenfd error!\n");
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    //servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_addr.s_addr = inet_addr(SERV_IP);
    servaddr.sin_port        = htons(SERV_PORT);

    if(bind(listenfd, (struct sockaddr*) &servaddr, sizeof(servaddr)) < 0)
        err_sys("bind error!\n");

    if(listen(listenfd, LISTENQ) < 0)
        err_sys("listen error!\n");

    printf("server is listening....\n");

    for ( ; ; ) {
        if((nfds = epoll_wait(epfd, evs, EVENT_ARR_SIZE, -1)) < 0)
            err_sys("epoll_wait error!\n");

        for(i = 0; i < nfds; i++)
        {
                if(evs[i].data.fd == listenfd)
                {
                    clilen = sizeof(cliaddr);
                    connfd = accept(listenfd, (struct sockaddr*) &cliaddr, &clilen);
                    if(connfd < 0)
                        continue;
    				fprintf(stdout, "get conn from %s\n", inet_ntoa(((struct sockaddr_in)cliaddr).sin_addr));             
                    setnonblocking(connfd);
                    ev.data.fd = connfd;
                    ev.events = EPOLLIN | EPOLLET;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) < 0)
                        err_sys("epoll_ctl connfd error!\n");            
                }
                else if(evs[i].events & EPOLLIN)
                {
                    sockfd = evs[i].data.fd;
                    if (sockfd < 0)
                        continue;
                    if ( (n = read(sockfd, buf, MAXLINE)) == 0) {
                        epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, &ev);
                        close(sockfd);
                        evs[i].data.fd = -1;
                    } 
                    else if(n < 0)
                        err_sys("read socket error!\n");
                    else
                    {
                        printf("write %d bytes\n", n);
                        write(sockfd, buf, n);
						/*fprintf(stdout, "close conn\n");	
						close(sockfd);
						epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, &ev);
						evs[i].data.fd = -1;*/
                    }
                }
                else
                    printf("other event!\n");
        }
    }
    return 0;
}


void setnonblocking(
    int sockfd
)
{
    int flag;
    
    flag = fcntl(sockfd, F_GETFL);
    if(flag < 0)
            err_sys("fcnt(F_GETFL) error!\n");
    flag |= O_NONBLOCK;
    if(fcntl(sockfd, F_SETFL, flag) < 0)
        err_sys("fcon(F_SETFL) error!\n");
}

