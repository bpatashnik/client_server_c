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
#include <time.h>

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
int start = 0;
FILE *fptr;
FILE *fptr2;
int file_size;
int num_full_chunks;
int total_chunks;
int bytes_left;

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
    int my_socket, slen, err;
    struct sockaddr_in sa, sa_client;
    char buf[1024];
    int BUFLEN;

    my_socket = socket(AF_INET, SOCK_DGRAM, 0);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(UDPPORT);
    inet_pton(AF_INET, IP, &(sa.sin_addr));
    bind(my_socket, (struct sockaddr *)&sa, sizeof(sa));
    slen = sizeof(sa_client);

    printf("UDP Socket Created on Port %d\n", UDPPORT);

    while (UDP_State == NOT_READY)
        sched_yield();

    //Client Acknowledgement
    err = recvfrom(my_socket, (char *)buf, 1024, 0, (struct sockaddr *)&sa_client, &slen);
    buf[err] = '\0';

    printf("UDP Server: Client Says %s\n", buf);

    time_t start = clock();
    //Main UDP Loop
    while (All_Received == 0)
    {
        printf("UDP Server: Sending Chunks to Client...\n");

        if (gap < 0)
            start = 0;
        else
            start = gap;

        //If SACK indicates missing chunk, send it
        for (int b = start; b < total_chunks; b++)
        {
            if (AckMessage[b] == '0')
            {
                struct chunk m = chunks[b];
                sendto(my_socket, &m, sizeof(m), 0, (struct sockaddr *)&sa_client, slen);
            }
        }

        printf("UDP Server: Chunks Sent\n");
        DONE_SENDING = 1;
        UDP_State = 1;
        gap = -1;

        //Block UDP until TCP done communicating with client
        while (UDP_State == DONE_SENDING && All_Received == 0)
            sched_yield();
    }
    time_t stop = clock();
    double time_taken = ((double)(stop - start)) / CLOCKS_PER_SEC;
    printf("Time Elapsed for File Transfer: %f Seconds\n", time_taken);
}

//TCP Thread
void *tcp_func(void *ptr)
{
    //TCP Initialization
    int socket_desc, client_sock, c, read_size;
    struct sockaddr_in server, client;
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    client_sock = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_family = AF_INET;
    server.sin_port = htons(TCPPORT);
    inet_pton(AF_INET, IP, &(server.sin_addr));
    bind(socket_desc, (struct sockaddr *)&server, sizeof(server));
    c = sizeof(struct sockaddr_in);
    listen(socket_desc, 1);
    client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t *)&c);

    printf("TCP Socket Created on Port %d\n", TCPPORT);

    send(client_sock, &file_size, sizeof(file_size), 0);
    send(client_sock, &num_full_chunks, sizeof(num_full_chunks), 0);
    send(client_sock, &total_chunks, sizeof(total_chunks), 0);
    send(client_sock, &bytes_left, sizeof(bytes_left), 0);

    recv(client_sock, &AckMessage, sizeof(AckMessage), MSG_WAITALL);

    NOT_READY = 0;

    //Main TCP Loop
    while (All_Received == 0)
    {
        //Block TCP until UDP finished sending
        while (UDP_State != DONE_SENDING)
            sched_yield();

        char All_Sent[] = "All Messages Sent";

        //Tell client all messages sent
        send(client_sock, &All_Sent, sizeof(All_Sent), 0);

        //Receive ACK array from client
        int recieved_amount = recv(client_sock, &AckMessage, sizeof(AckMessage), MSG_WAITALL);

        printf("TCP Server: ACK Bytes Received: %d\n", recieved_amount);

        //Determine first gap in ACK array
        for (int c = 0; c < num_full_chunks; c++)
        {
            if (AckMessage[c] != '1')
            {
                gap = c;
                break;
            }
        }

        //If gap in ACK exists, return to UDP send loop
        if (gap != -1)
        {
            printf("TCP Server: Client Did Not Receive Chunk %d\n", gap);
            DONE_SENDING = 0;
            UDP_State = -1;
        }

        //If gap in ACK does not exist, all chunks receieved by client. Done!
        else
        {
            printf("TCP Server: All Chunks Received by Client!\n");
            All_Received = 1;
        }
    }
}

int main()
{
    fptr = fopen("BitMap.txt", "r");

    if (fptr == NULL)
    {
        printf("File not found.");
        return 1;
    }

    //Determine file size
    fseek(fptr, 0, SEEK_END);
    file_size = ftell(fptr);
    fseek(fptr, 0, SEEK_SET);

    num_full_chunks = file_size / 4096;

    bytes_left = file_size - (num_full_chunks * 4096);

    chunks = malloc((sizeof(struct chunk) * num_full_chunks) + (sizeof(struct chunk) + bytes_left));

    //fptr2 = fopen("ReceivedFile.txt", "w");

    for (int k = 0; k < num_full_chunks; k++)
    {
        chunks[k].header = k;
        fread(chunks[k].data, 4096, 1, fptr);
        //fwrite(chunks[k].data, 4096, 1, fptr2);
    }

    chunks[num_full_chunks].header = num_full_chunks;
    fread(chunks[num_full_chunks].data, bytes_left, 1, fptr);

    //fwrite(chunks[num_full_chunks].data, bytes_left, 1, fptr2);
    //fclose(fptr2);

    total_chunks = num_full_chunks + 1;

    pthread_t udp, tcp;
    int ret_udp, ret_tcp;

    pthread_create(&tcp, NULL, tcp_func, NULL);
    pthread_create(&udp, NULL, udp_func, NULL);

    pthread_join(tcp, NULL);
    pthread_join(udp, NULL);

    return 0;
}
