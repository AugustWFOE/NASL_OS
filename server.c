#define LONESHA256_STATIC
#include "lonesha256.h"
#include "messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdint.h>
#include <string.h>
#include <endian.h>
#include <semaphore.h>

sem_t thread_slots;

void *answeringMachine(void *client);
uint64_t loneshaSolver(uint64_t start, uint64_t end, const uint8_t *target_hash);
int checkRequestSize(int client_fd, uint8_t *req_buf);


struct request {
    uint64_t start;
    uint64_t end;
    uint8_t hash[32];
    uint64_t waste;
    int client_fd;
};

int main(int argc, char *argv[])
{   
    if (argc < 2)
    {
        printf("Please provide port number");
        return 1;
    };

    uint16_t port_number = (uint16_t)atoi(argv[1]); // Perhaps this should be validated

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in address;

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_number);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) == -1)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Server listening on port %u\n", port_number);

    while (1)
    {
       int client_fd = accept(server_fd, NULL, NULL);

        if (client_fd == -1)
        {
            perror("accept");
            continue;
        }
        pthread_t thread;
        //

        printf("Client connected!\n");
        
        uint8_t req_buf[PACKET_REQUEST_SIZE];

        
        if (checkRequestSize(client_fd, req_buf) != 1)
        {
            close(client_fd);
            continue;
        }
        printf("Request successfully received!\n");

        uint64_t start_net, end_net;
        memcpy(&start_net, &req_buf[PACKET_REQUEST_START_OFFSET], sizeof(uint64_t));
        memcpy(&end_net, &req_buf[PACKET_REQUEST_END_OFFSET], sizeof(uint64_t));
        
        uint8_t priority = req_buf[PACKET_REQUEST_PRIO_OFFSET];

        struct request *newrequest = malloc(sizeof(struct request));

        if (newrequest == NULL)
        {
            perror("malloc");
            close(client_fd);
            continue;
        }

        newrequest->start = be64toh(start_net);
        newrequest->end = be64toh(end_net);
        newrequest->client_fd = client_fd;
        newrequest->waste = (newrequest->end - newrequest->start) / priority;
        memcpy(
            newrequest->hash, &req_buf[PACKET_REQUEST_HASH_OFFSET], 32);
        

        //
        sem_wait(&thread_slots);

        if (pthread_create(&thread, NULL, answeringMachine, newrequest) != 0)
        {
            perror("pthread_create");

            sem_post(&thread_slots);

            close(client_fd);
            free(newrequest);
        }
        else
        {
            pthread_detach(thread);
        }
    }

    close(server_fd);
    return 0;
}

void *answeringMachine(void *arg)
{
    printf("Worker thread started!\n");
    struct request *newrequest = (struct request *)arg;
    printf(
        "Searching from %llu to %llu\n",
        (unsigned long long)newrequest->start,
        (unsigned long long)newrequest->end
    );
    uint64_t answer = loneshaSolver(
        newrequest->start,
        newrequest->end,
        newrequest->hash
    );

    uint64_t answer_net = htobe64(answer);

    ssize_t bytes = send(
        newrequest->client_fd,
        &answer_net,
        PACKET_RESPONSE_SIZE,
        MSG_NOSIGNAL
    );

    printf("[server] %llu\n",
           (unsigned long long)answer);

    close(newrequest->client_fd);

    free(newrequest);

    sem_post(&thread_slots);

    return NULL;
}

uint64_t loneshaSolver(uint64_t start, uint64_t end,
                       const uint8_t *target_hash)
{
    uint8_t hash[32];

    if (start > end)
        return 0;

    for (uint64_t i = start; ; i++)
    {
        uint64_t le_val = htole64(i);

        lonesha256(hash,
                   (const unsigned char *)&le_val,
                   sizeof(le_val));

        if (memcmp(hash, target_hash, 32) == 0)
            return i;

        if (i == end)
            break;
    }

    return 0;
}

int checkRequestSize(int client_fd, uint8_t *req_buf){
    ssize_t total_read = 0;
        while (total_read < PACKET_REQUEST_SIZE) {
            ssize_t bytes = read(client_fd, req_buf + total_read, PACKET_REQUEST_SIZE - total_read);
            if (bytes <= 0) {
                break;
            }
            total_read += bytes;
        }

        if (total_read < PACKET_REQUEST_SIZE)
        {
            return 0;
        }

        return 1;
}