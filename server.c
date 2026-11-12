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



//sem_t thread_slots;

void *adventurer(void *client); //answeringMachine
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
    struct request *data;
    struct Node *next;
};

struct PriorityQueue {
    struct Node *head;
    struct Node *tail;
};

struct PriorityQueue queues[16] = {0};

int requestScheduler(struct request *req);
struct request *getRequest(void);
int isQueueEmpty(void);

#define MAX_THREADS 2
pthread_t party[MAX_THREADS]; //(workers)

pthread_mutex_t quest_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t quest_available = PTHREAD_COND_INITIALIZER;

//pthread_mutex_t answer_mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char *argv[])
{   


    if (argc < 2)
    {
        printf("Please provide port number");
        return 1;
    };


    // if (sem_init(&thread_slots, 0, MAX_THREADS) == -1)
    // {
    //     perror("sem_init");
    //     return 1;
    // }

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


    for (int i = 0; i < MAX_THREADS; i++){
        pthread_create(&party[i], NULL, adventurer, NULL);
    } //Should probably handle errors

    printf("Party (workers) created!");
    

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
        pthread_mutex_lock(&quest_mutex);

        requestScheduler(newrequest);

        pthread_cond_signal(&quest_available);

        pthread_mutex_unlock(&quest_mutex);
        

        
        
        //Right now creating threads at the beginning

        // sem_wait(&thread_slots);

        // if (pthread_create(&thread, NULL, adventurer, getRequest()) != 0)
        // {
        //     perror("pthread_create");

        //     sem_post(&thread_slots);

        //     close(client_fd);
        //     free(newrequest);
        // }
        // else
        // {
        //     pthread_detach(thread);
        // }
    }

    close(server_fd);
    return 0;
}

void *adventurer(void *arg) {

    (void)arg;

    while(1) {

        pthread_mutex_lock(&quest_mutex);

        while(isQueueEmpt()){
            pthread_cond_wait(&quest_available, &quest_mutex);
        }


    
        struct request *quest = getRequest();

        pthread_mutex_unlock(&quest_mutex);

        printf("Quest accepted!\n");
        
        printf(
            "Searching from %llu to %llu\n",
            (unsigned long long)quest->start,
            (unsigned long long)quest->end
        );
    
        uint64_t answer = loneshaSolver(
            quest->start,
            quest->end,
            quest->hash
        );

    uint64_t answer_net = htobe64(answer);
    
    size_t total_sent = 0;
    const uint8_t *buf = (const uint8_t *)&answer_net;

    while (total_sent < PACKET_RESPONSE_SIZE)
    {
        ssize_t bytes = send(
            quest->client_fd,
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

        close(quest->client_fd);

        free(quest);

    } 
           
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

int requestScheduler(struct request *req)
{
    uint8_t p = req->prio;

    if (p >= 16)
        return -1;

    struct Node *node = malloc(sizeof(struct Node));
    if (node == NULL)
        return -1;

    node->data = req;
    node->next = NULL;

    struct PriorityQueue *q = &queues[p];

    if (q->tail == NULL)
    {
        q->head = node;
        q->tail = node;
    }
    else
    {
        q->tail->next = node;
        q->tail = node;
    }

    return 0;
}

struct request *getRequest(void)
{
    for (int p = 15; p >= 0; p--)
    {
        struct PriorityQueue *q = &queues[p];

        if (q->head == NULL)
            continue;

        struct Node *node = q->head;
        struct request *req = node->data;

        q->head = node->next;

        if (q->head == NULL)
            q->tail = NULL;

        free(node);
        return req;
    }

    return NULL;
}


int isQueueEmpty(void){
    for (int p = 0; p < 16; p++){
        if(queues[p].head != NULL) {
            return 0;
        }
    }
    return 1;
}