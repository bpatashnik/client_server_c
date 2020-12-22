#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <sched.h>

#define TCPPORT 45350
#define UDPPORT 45250
#define IP "130.111.46.105"

//Global Variables
char AckMessage[1563];
int UDP_State = -1;
int NOT_READY = -1;
int DONE_SENDING = 0;
int All_Received = 0;
int gap = -1;
FILE *fptr2;
int file_size;
int num_full_chunks;
int total_chunks;
int trailing_bytes;

struct chunk
{
    int header;
    char data[4096];
};

struct chunk *chunks;

//UDP Thread
void *udp_func(void *ptr)
{
    //UDP Initialization
    struct sockaddr_in sa;
    socklen_t slen = sizeof(sa);
    int my_sock = socket(AF_INET, SOCK_DGRAM, 0);
    int flags = fcntl(my_sock, F_GETFL, 0);
    fcntl(my_sock, F_SETFL, flags | O_NONBLOCK);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(UDPPORT);
    inet_pton(AF_INET, IP, &(sa.sin_addr));

    //Initial message for server
    char *msg = "Ready to Receive Messages";
    sendto(my_sock, (const char *)msg, strlen(msg), 0, (struct sockaddr *)&sa, slen);

    //Block UDP until TCP done initializing with server
    while (UDP_State == NOT_READY)
        sched_yield();

    struct chunk m;

    //Main UDP Loop
    while (All_Received == 0)
    {

        printf("UDP Client: Entering Receive Loop...\n");

        //Receive chunks from server
        for (int b = 0; b < 1000; b++)
        {
            recvfrom(my_sock, &m, sizeof(m), 0, (struct sockaddr *)&sa, &slen);
            chunks[m.header].header = m.header;
            strcpy(chunks[m.header].data, m.data);
            AckMessage[m.header] = '1';
        }

        printf("UDP Client: Exiting Receive Loop...\n");

        DONE_SENDING = 1;
        UDP_State = 1;
        gap = -1;

        //Block UDP until TCP done communicating with server
        while (UDP_State == DONE_SENDING && All_Received == 0)
            sched_yield();
    }
}

//TCP Thread
void *tcp_func(void *ptr)
{
    //TCP Initialization
    struct sockaddr_in sa;
    char All_Sent[18];
    int my_sock = socket(AF_INET, SOCK_STREAM, 0);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(TCPPORT);
    inet_pton(AF_INET, IP, &(sa.sin_addr));
    connect(my_sock, (struct sockaddr *)&sa, sizeof(sa));

    recv(my_sock, &file_size, sizeof(file_size), MSG_WAITALL);
    recv(my_sock, &num_full_chunks, sizeof(num_full_chunks), MSG_WAITALL);
    recv(my_sock, &total_chunks, sizeof(total_chunks), MSG_WAITALL);
    recv(my_sock, &trailing_bytes, sizeof(trailing_bytes), MSG_WAITALL);

    printf("Receiving File of Size %d Bytes from Server\n", file_size);
    printf("Receiving %d Full Chunks Containing 4096 Bytes from Server\n", num_full_chunks);
    printf("Receiving 1 Incomplete Chunk Containing %d Bytes from Server\n", trailing_bytes);
    printf("Receiving %d Total Chunks from Server\n", total_chunks);

    chunks = malloc((sizeof(struct chunk) * num_full_chunks) + (sizeof(struct chunk) + trailing_bytes));

    for (int e = 0; e < total_chunks; e++)
    {
        AckMessage[e] = '0';
    }

    send(my_sock, &AckMessage, sizeof(AckMessage), 0);

    NOT_READY = 0;

    //Main TCP Loop
    while (All_Received == 0)
    {

        //Block TCP until UDP finished receiving
        while (UDP_State != DONE_SENDING)
            sched_yield();

        //Receive all sent message from server
        recv(my_sock, &All_Sent, sizeof(All_Sent), 0);

        printf("TCP Client: Server Says %s\n", All_Sent);

        //Send ACK array to server
        int sent_amount = send(my_sock, &AckMessage, sizeof(AckMessage), 0);
        printf("TCP Client: Sending ACK to TCP Server (%d bytes)...\n", sent_amount);

        //Determine first gap in ACK array
        for (int c = 0; c < total_chunks; c++)
        {
            if (AckMessage[c] != '1')
            {
                gap = c;
                break;
            }
        }

        //If gap in ACK exists, return to UDP receive loop
        if (gap != -1)
        {
            DONE_SENDING = 0;
            UDP_State = -1;
        }

        //If gap in ACK does not exist, all chunks receieved. Done!
        else
        {
            printf("TCP Client: All Chunks Received! Exiting...\n");
            All_Received = 1;
        }
    }
}

int main()
{

    pthread_t udp, tcp;
    int ret_udp, ret_tcp;

    pthread_create(&tcp, NULL, tcp_func, NULL);
    pthread_create(&udp, NULL, udp_func, NULL);

    pthread_join(tcp, NULL);
    pthread_join(udp, NULL);

    fptr2 = fopen("ReceivedFile.txt", "w");

    for (int z = 0; z < num_full_chunks; z++)
    {
        fwrite(chunks[z].data, 4096, 1, fptr2);
    }

    fwrite(chunks[num_full_chunks].data, trailing_bytes, 1, fptr2);
    fclose(fptr2);

    return 0;
}
