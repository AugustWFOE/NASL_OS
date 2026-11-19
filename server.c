#define LONESHA256_STATIC
#include "lonesha256.h"
#include "messages.h"
#include "hashtable.h"
#include "hashtable.c"

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


void *adventurer(void *client); 
uint64_t loneshaSolver(uint64_t start, uint64_t end, const uint8_t *target_hash);

struct QueueNode
{
    uint8_t smallPackage[45];
    struct QueueNode *next;
};

struct PriorityQueue
{
    struct QueueNode *head;
    struct QueueNode *tail;
};

struct PriorityQueue queues[16] = {0};

int requestScheduler(struct request *req);

int isQueueEmpty(void);

#define MAX_THREADS 4
pthread_t party[MAX_THREADS]; //(workers)

pthread_mutex_t quest_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t quest_available = PTHREAD_COND_INITIALIZER;

pthread_mutex_t database_mutex = PTHREAD_MUTEX_INITIALIZER;


HashTable database = {0};

int main(int argc, char *argv[])
{
    if (database.buckets == NULL)
    {
        if (create_hashtable(&database, 1024) != 0)
        {
            return 1;
        }
    }

    if (argc < 2)
    {
        printf("Please provide port number");
        return 1;
    };

    uint16_t port_number = (uint16_t)atoi(argv[1]); // Perhaps this should be validated

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in address;

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_number);


    for (int i = 0; i < MAX_THREADS; i++)
    {
        pthread_create(&party[i], NULL, adventurer, NULL);
    } 

    while (1)
    {
        int client_fd = accept(server_fd, NULL, NULL);

        uint8_t smallPackage[45];
        memcpy(&smallPackage, &req_buf[0], 40);
        memcpy(&smallPackage[40], &client_fd,4);
        memcpy(&smallPackage[44], &req_buf[PACKET_REQUEST_PRIO_OFFSET],1);
        
        pthread_mutex_lock(&quest_mutex);

        requestScheduler(&smallPackage);

        pthread_cond_signal(&quest_available);

        pthread_mutex_unlock(&quest_mutex);
    }

    close(server_fd);
    return 0;
}

void *adventurer(void *arg)
{

    (void)arg;

    while (1)
    {

        pthread_mutex_lock(&quest_mutex);

        while (isQueueEmpty())
        {
            pthread_cond_wait(&quest_available, &quest_mutex);
        }

        uint8_t quest[44];
        memcpy(&quest[44], getRequest(),44);

        pthread_mutex_unlock(&quest_mutex);

        printf("Quest accepted!\n");

        uint64_t answer;
        int found_answer = 0;

        pthread_mutex_lock(&database_mutex);
        uint64_t *search = lookup(&database, *quest[32]);
        

        if (search != NULL && *search >= quest->start && *search <= quest->end)
        {
            answer = *search;
            found_answer = 1;
        }
        pthread_mutex_unlock(&database_mutex);

        if(found_answer == 0)
        {
            answer = loneshaSolver(
                &quest[44]
            );
        }

        uint64_t answer_net = htobe64(answer);

        size_t total_sent = 0;
        const uint8_t *buf = (const uint8_t *)&answer_net;

        while (total_sent < PACKET_RESPONSE_SIZE)
        {
            ssize_t bytes = send(
                quest->client_fd,
                buf + total_sent,
                PACKET_RESPONSE_SIZE - total_sent,
                MSG_NOSIGNAL);

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

uint64_t loneshaSolver(const uint8_t target_hash[44])
{
    uint8_t hash[32];

    if (start > end)
        return 0;

    for (uint64_t i = start;; i++)
    {
        uint64_t le_val = htole64(i);

        lonesha256(hash,
                   (const unsigned char *)&le_val,
                   sizeof(le_val));

        if (memcmp(hash, target_hash, 32) == 0){
            pthread_mutex_lock(&database_mutex);
            insert(&database, hash, i);
            pthread_mutex_unlock(&database_mutex);
            return i;
        }
            

        if (i == end)
            break;
    }

    return 0;
}

int requestScheduler(uint8_t[] &smallPackage)
{
    uint8_t p = *smallPackage[45];

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

        struct QueueNode *node = q->head;
        uint8_t[44] &newPackage;
        memcpy(&newPackage, q->&smallPackage[0] ,44)

        q->head = node->next;

        if (q->head == NULL)
            q->tail = NULL;

        free(node);
        return &newPackage;
    }

    return NULL;
}

int isQueueEmpty(void)
{
    for (int p = 0; p < 16; p++)
    {
        if (queues[p].head != NULL)
        {
            return 0;
        }
    }
    return 1;
}