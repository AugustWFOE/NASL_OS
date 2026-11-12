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
#include <math.h>

#include <errno.h>



sem_t thread_slots;

void *answeringMachine(void *client);
uint64_t loneshaSolver(uint64_t start, uint64_t end, const uint8_t *target_hash);
int checkRequestSize(int client_fd, uint8_t *req_buf);
uint64_t queueFunc(uint64_t interval, uint8_t priority);

struct request {
    uint64_t start;
    uint64_t end;
    uint8_t hash[32];
    uint64_t interval;
    uint8_t prio;
    int client_fd;
};
struct Node {
    struct request* data;
    uint64_t value;
    struct Node* next;
    struct Node* prev;
};
struct Node *headPointer = NULL;

void requestScheduler(void *arg);
struct request *getRequest(void);

#define MAX_THREADS 2

int main(int argc, char *argv[])
{   
    if (argc < 2)
    {
        printf("Please provide port number");
        return 1;
    };
    if (sem_init(&thread_slots, 0, MAX_THREADS) == -1)
    {
        perror("sem_init");
        return 1;
    }

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
        newrequest->interval = (newrequest->end - newrequest->start);
        newrequest->prio = priority;
        memcpy(
            newrequest->hash, &req_buf[PACKET_REQUEST_HASH_OFFSET], 32);
        
        //Priotime?
        requestScheduler(newrequest);

        if (sem_trywait(&thread_slots) == 0)
        {
            struct request *nextRequest = getRequest();

            int err = pthread_create(
                &thread,
                NULL,
                answeringMachine,
                nextRequest
            );

            if (err != 0)
            {
                fprintf(stderr, "pthread_create: %s\n", strerror(err));
                // Handle nextRequest rather than losing it
                sem_post(&thread_slots);
            }
            else
            {
                pthread_detach(thread);
            }
        }
    }

    close(server_fd);
    return 0;
}

void *answeringMachine(void *arg)
{
    printf("Worker thread started!\n");
    struct request *newrequest = (struct request *)arg;
    uint64_t answer = loneshaSolver(
        newrequest->start,
        newrequest->end,
        newrequest->hash
    );

    uint64_t answer_net = htobe64(answer);
    
    size_t total_sent = 0;
    const uint8_t *buf = (const uint8_t *)&answer_net;

    while (total_sent < PACKET_RESPONSE_SIZE)
    {
        ssize_t bytes = send(
            newrequest->client_fd,
            buf + total_sent,
            PACKET_RESPONSE_SIZE - total_sent,
            MSG_NOSIGNAL
        );

        if (bytes < 0)
        {
            if (errno == EINTR)
                continue;

            perror("send");
            break;
        }

        if (bytes == 0)
        {
            fprintf(stderr, "send returned 0\n");
            break;
        }

        total_sent += (size_t)bytes;
    }

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

uint64_t queueFunc(uint64_t interval, uint8_t priority){
    return priority;
}

// uint64_t queueFunc(uint64_t interval, uint8_t priority){
//     return interval * priority * priority;
// }

// uint64_t queueFunc(uint64_t interval, uint8_t priority){
//     return (uint64_t)sqrt((double)interval) * priority;
// }

// uint64_t queueFunc(uint64_t interval, uint8_t priority){
//     if (interval <= 1) return priority;
//     return (uint64_t)log2((double)interval) * priority;
// }

// uint64_t queueFunc(uint64_t interval, uint8_t priority){
//     if (interval <= 1) return priority;
//     return (uint64_t)log2(log2((double)interval + 2.0) +1.0) * priority;
// }

// uint64_t queueFunc(uint64_t interval, uint8_t priority){
//     if (interval <= 1) return priority;

//     double inverse_range = 1000000.0 / log2((double)interval);
//     return (uint64_t)(inverse_range * (double)priority);
// }

void requestScheduler(void *arg)
{
    struct request *newrequest = (struct request *)arg;

    struct Node *newNode = malloc(sizeof(struct Node));
    if (newNode == NULL)
        return;

    newNode->value = queueFunc(newrequest->interval,
                               newrequest->prio);
    newNode->data = newrequest;

    if (headPointer == NULL)
    {
        headPointer = newNode;
        newNode->next = newNode;
        newNode->prev = newNode;
        return;
    }

    struct Node *current = headPointer;

    // Find the first node with a smaller value
    while (current->value >= newNode->value)
    {
        current = current->next;

        if (current == headPointer)
            break;
    }

    // Insert immediately before current
    newNode->next = current;
    newNode->prev = current->prev;

    current->prev->next = newNode;
    current->prev = newNode;

    // Update head if the new node is the largest
    if (newNode->value > headPointer->value)
    {
        headPointer = newNode;
    }
}

struct request *getRequest(void)
{
    if (headPointer == NULL)
        return NULL;

    struct Node *oldHead = headPointer;
    struct request *temp = oldHead->data;

    if (oldHead->next == oldHead)
    {
        headPointer = NULL;
    }
    else
    {
        oldHead->prev->next = oldHead->next;
        oldHead->next->prev = oldHead->prev;

        headPointer = oldHead->next;
    }

    free(oldHead);

    return temp;
}