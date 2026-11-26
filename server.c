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
#include <errno.h>

#define MAX_THREADS 4
#define NUM_PRIORITIES 16
#define QUEUE_CAPACITY 4096

#define HASH_OFFSET  0
#define START_OFFSET 32
#define END_OFFSET   40
#define FD_OFFSET    48
#define PRIO_OFFSET  (FD_OFFSET + sizeof(int))
#define PACKAGE_SIZE (PRIO_OFFSET + 1)

uint8_t queues[NUM_PRIORITIES][QUEUE_CAPACITY][PACKAGE_SIZE];

unsigned int head[NUM_PRIORITIES] = {0};
unsigned int tail[NUM_PRIORITIES] = {0};
unsigned int count[NUM_PRIORITIES] = {0};

pthread_t party[MAX_THREADS];

pthread_mutex_t quest_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t quest_available = PTHREAD_COND_INITIALIZER;
pthread_cond_t queue_space = PTHREAD_COND_INITIALIZER;

pthread_mutex_t database_mutex = PTHREAD_MUTEX_INITIALIZER;

HashTable database = {0};

void *adventurer(void *arg);
uint64_t loneshaSolver(const uint8_t *packet);

int requestScheduler(const uint8_t *packet);
int getRequest(uint8_t *packet);
int isQueueEmpty(void);


int main(int argc, char *argv[])
{
    if (create_hashtable(&database, 1024) != 0)
        return 1;

    if (argc < 2)
    {
        printf("Please provide port number\n");
        return 1;
    }

    uint16_t port_number = (uint16_t)atoi(argv[1]);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    int opt = 1;

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));

    struct sockaddr_in address = {0};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_number);

    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    for (int i = 0; i < MAX_THREADS; i++)
    {
        if (pthread_create(&party[i], NULL,
                           adventurer, NULL) != 0)
        {
            fprintf(stderr, "Could not create worker\n");
            return 1;
        }
    }

    while (1)
    {
        int client_fd = accept(server_fd, NULL, NULL);

        if (client_fd < 0)
        {
            if (errno == EINTR)
                continue;

            perror("accept");
            continue;
        }

        uint8_t req_buf[PACKET_REQUEST_SIZE];
        size_t received = 0;

        while (received < PACKET_REQUEST_SIZE)
        {
            ssize_t bytes = recv(
                client_fd,
                req_buf + received,
                PACKET_REQUEST_SIZE - received,
                0
            );

            if (bytes < 0 && errno == EINTR)
                continue;

            if (bytes <= 0)
                break;

            received += (size_t)bytes;
        }

        uint8_t smallPackage[PACKAGE_SIZE];

        memcpy(smallPackage, req_buf, 48);

        memcpy(smallPackage + FD_OFFSET,
               &client_fd, sizeof(client_fd));

        smallPackage[PRIO_OFFSET] =
            req_buf[PACKET_REQUEST_PRIO_OFFSET];

        uint8_t p = smallPackage[PRIO_OFFSET];

        if (p >= NUM_PRIORITIES)
        {
            close(client_fd);
            continue;
        }

        pthread_mutex_lock(&quest_mutex);

        while (count[p] == QUEUE_CAPACITY)
        {
            pthread_cond_wait(&queue_space,
                              &quest_mutex);
        }

        requestScheduler(smallPackage);

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
        uint8_t quest[PACKAGE_SIZE];

        pthread_mutex_lock(&quest_mutex);

        while (isQueueEmpty())
        {
            pthread_cond_wait(&quest_available,
                              &quest_mutex);
        }

        getRequest(quest);

        pthread_cond_signal(&queue_space);

        pthread_mutex_unlock(&quest_mutex);

        int client_fd;

        memcpy(&client_fd, quest + FD_OFFSET,
               sizeof(client_fd));

        uint64_t start, end;

        memcpy(&start, quest + START_OFFSET, 8);
        memcpy(&end, quest + END_OFFSET, 8);

        start = be64toh(start);
        end = be64toh(end);

        uint64_t answer = 0;
        int found_answer = 0;

        pthread_mutex_lock(&database_mutex);

        uint64_t *search = lookup(
            &database,
            quest + HASH_OFFSET
        );

        if (search != NULL)
        {
            if (*search >= start && *search <= end)
            {
                answer = *search;
                found_answer = 1;
            }
        }

        pthread_mutex_unlock(&database_mutex);

        if (!found_answer)
        {
            answer = loneshaSolver(quest);
        }

        uint64_t answer_net = htobe64(answer);

        size_t total_sent = 0;

        const uint8_t *buf =
            (const uint8_t *)&answer_net;

        while (total_sent < PACKET_RESPONSE_SIZE)
        {
            ssize_t bytes = send(
                client_fd,
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
                break;

            total_sent += (size_t)bytes;
        }

        close(client_fd);
    }

    return NULL;
}


uint64_t loneshaSolver(const uint8_t *packet)
{
    uint8_t hash[32];
    uint64_t start, end;

    memcpy(&start, packet + START_OFFSET, 8);
    memcpy(&end, packet + END_OFFSET, 8);

    start = be64toh(start);
    end = be64toh(end);

    if (start > end)
        return 0;

    for (uint64_t i = start; ; i++)
    {
        uint64_t le_val = htole64(i);

        lonesha256(
            hash,
            (const unsigned char *)&le_val,
            sizeof(le_val)
        );

        if (memcmp(hash, packet + HASH_OFFSET, 32) == 0)
        {
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


int requestScheduler(const uint8_t *packet)
{
    uint8_t p = packet[PRIO_OFFSET];

    if (p >= NUM_PRIORITIES)
        return -1;

    if (count[p] == QUEUE_CAPACITY)
        return -1;

    memcpy(
        queues[p][tail[p]],
        packet,
        PACKAGE_SIZE
    );

    tail[p] = (tail[p] + 1) % QUEUE_CAPACITY;

    count[p]++;

    return 0;
}


int getRequest(uint8_t *packet)
{
    for (int p = NUM_PRIORITIES - 1; p >= 0; p--)
    {
        if (count[p] == 0)
            continue;

        memcpy(
            packet,
            queues[p][head[p]],
            PACKAGE_SIZE
        );

        head[p] = (head[p] + 1) % QUEUE_CAPACITY;

        count[p]--;

        return 0;
    }

    return -1;
}


int isQueueEmpty(void)
{
    for (int p = 0; p < NUM_PRIORITIES; p++)
    {
        if (count[p] != 0)
            return 0;
    }

    return 1;
}