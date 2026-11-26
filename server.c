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
#include <stdbool.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

#define MAX_SIZE 1000
#define SOLUTION_SIZE (sizeof(int) + sizeof(uint64_t))
#define POLL_TIMEOUT 1
#define MAX_THREADS 4

void *adventurer(void *client);

uint64_t loneshaSolver(
    uint64_t start,
    uint64_t end,
    const uint8_t *target_hash
);

int checkRequestSize(int client_fd, uint8_t *req_buf);


struct request
{
    uint64_t start;
    uint64_t end;
    uint8_t hash[32];
    uint8_t prio;
    int client_fd;
};


struct QueueNode
{
    struct request *data;
    struct QueueNode *next;
};
uint64_t counter = 0;


typedef struct
{
    uint8_t array[MAX_SIZE][SOLUTION_SIZE];
    int top;
} Stack;

Stack stack = {.top = -1};


struct PriorityQueue
{
    struct QueueNode *head;
    struct QueueNode *tail;
};

struct PriorityQueue queues[16] = {0};


int requestScheduler(struct request *req);
struct request *getRequest(void);
int isQueueEmpty(void);

int push(Stack *stack, const uint8_t *package);
int pop(Stack *stack, uint8_t *package);
bool isEmpty(Stack *stack);

void sendAnswer(int client_fd, uint64_t answer);
void sendCompletedAnswers(void);


pthread_t party[MAX_THREADS];

pthread_mutex_t quest_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t quest_available = PTHREAD_COND_INITIALIZER;

pthread_mutex_t database_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t stack_mutex = PTHREAD_MUTEX_INITIALIZER;

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
        printf("Please provide port number\n");
        return 1;
    }

    uint16_t port_number = (uint16_t)atoi(argv[1]);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in address = {0};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_number);

    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) == -1)
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

    // NEW: Make the listening socket non-blocking.

    int flags = fcntl(server_fd, F_GETFL, 0);

    if (flags == -1 ||
        fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        perror("fcntl");
        close(server_fd);
        return 1;
    }

    // NEW: Create the poll descriptor.

    struct pollfd listener = {
        .fd = server_fd,
        .events = POLLIN
    };

    printf("Server listening on port %u\n", port_number);

    for (int i = 0; i < MAX_THREADS; i++)
    {
        int result = pthread_create(
            &party[i],
            NULL,
            adventurer,
            NULL
        );

        if (result != 0)
        {
            fprintf(stderr, "pthread_create: %s\n",
                    strerror(result));

            close(server_fd);
            return 1;
        }
    }

    printf("Party (workers) created!\n");

    while (1)
    {
        // NEW: Send all currently completed answers.

        sendCompletedAnswers();

        // NEW: Wait for an incoming connection,
        // but only for POLL_TIMEOUT milliseconds.

        int ready = poll(&listener, 1, POLL_TIMEOUT);

        if (ready < 0)
        {
            if (errno == EINTR)
                continue;

            perror("poll");
            break;
        }

        if (ready == 0)
            continue;

        if (listener.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            fprintf(stderr, "Listening socket error\n");
            break;
        }

        if (!(listener.revents & POLLIN))
            continue;

        int client_fd = accept(server_fd, NULL, NULL);

        if (client_fd == -1)
        {
            if (errno == EAGAIN ||
                errno == EWOULDBLOCK ||
                errno == EINTR)
            {
                continue;
            }

            perror("accept");
            continue;
        }

        uint8_t req_buf[PACKET_REQUEST_SIZE];

        if (checkRequestSize(client_fd, req_buf) != 1)
        {
            close(client_fd);
            continue;
        }

        uint64_t start_net, end_net;

        memcpy(
            &start_net,
            req_buf + PACKET_REQUEST_START_OFFSET,
            sizeof(uint64_t)
        );

        memcpy(
            &end_net,
            req_buf + PACKET_REQUEST_END_OFFSET,
            sizeof(uint64_t)
        );

        uint64_t start = be64toh(start_net);
        uint64_t end = be64toh(end_net);

        uint8_t priority =
            req_buf[PACKET_REQUEST_PRIO_OFFSET];

        // NEW: Validate priority before using it.

        if (priority < 1 || priority > 16)
        {
            close(client_fd);
            continue;
        }

        // CHANGED: Perform database lookup before
        // allocating and scheduling a new request.

        uint64_t cached_answer;
        bool found = false;

        pthread_mutex_lock(&database_mutex);

        uint64_t *search = lookup(
            &database,
            req_buf + PACKET_REQUEST_HASH_OFFSET
        );

        if (search != NULL &&
            *search >= start &&
            *search <= end)
        {
            cached_answer = *search;
            found = true;
        }

        pthread_mutex_unlock(&database_mutex);

        // If the answer is cached, respond immediately.

        if (found)
        {
            sendAnswer(client_fd, cached_answer);
            continue;
        }

        // Otherwise, create a new request.

        struct request *newrequest =
            malloc(sizeof(struct request));

        if (newrequest == NULL)
        {
            perror("malloc");
            close(client_fd);
            continue;
        }

        newrequest->start = start;
        newrequest->end = end;
        newrequest->client_fd = client_fd;
        newrequest->prio = priority;

        memcpy(
            newrequest->hash,
            req_buf + PACKET_REQUEST_HASH_OFFSET,
            32
        );

        pthread_mutex_lock(&quest_mutex);

        int result = requestScheduler(newrequest);

        if (result == 0)
        {
            pthread_cond_signal(&quest_available);
        }

        pthread_mutex_unlock(&quest_mutex);

        // NEW: Handle failed scheduling.

        if (result == -1)
        {
            free(newrequest);
            close(client_fd);
        }
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
            pthread_cond_wait(
                &quest_available,
                &quest_mutex
            );
        }

        struct request *quest = getRequest();

        pthread_mutex_unlock(&quest_mutex);

        uint64_t answer = loneshaSolver(
            quest->start,
            quest->end,
            quest->hash
        );

        uint8_t package[SOLUTION_SIZE];

        memcpy(
            package,
            &quest->client_fd,
            sizeof(int)
        );

        memcpy(
            package + sizeof(int),
            &answer,
            sizeof(uint64_t)
        );

        pthread_mutex_lock(&stack_mutex);

        int result = push(&stack, package);

        pthread_mutex_unlock(&stack_mutex);

        // Fallback if the solution stack is full.

        if (result == -1)
        {
            sendAnswer(quest->client_fd, answer);
        }

        free(quest);
    }

    return NULL;
}


uint64_t loneshaSolver(
    uint64_t start,
    uint64_t end,
    const uint8_t *target_hash)
{
    uint8_t hash[32];

    if (start > end)
        return 0;

    uint64_t first_bytes;

    memcpy(&first_bytes, target_hash, 8);

    for (uint64_t i = start; ; i++)
    {
        uint64_t le_val = htole64(i);

        lonesha256(
            hash,
            (const unsigned char *)&le_val,
            sizeof(le_val)
        );

        uint64_t first_bytes_of_hash;

        memcpy(&first_bytes_of_hash, hash, 8);

        if (first_bytes_of_hash == first_bytes &&
            memcmp(hash, target_hash, 32) == 0)
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


int checkRequestSize(int client_fd, uint8_t *req_buf)
{
    size_t total_read = 0;

    while (total_read < PACKET_REQUEST_SIZE)
    {
        ssize_t bytes = read(
            client_fd,
            req_buf + total_read,
            PACKET_REQUEST_SIZE - total_read
        );

        // NEW: Retry interrupted reads.

        if (bytes < 0 && errno == EINTR)
            continue;

        if (bytes <= 0)
            break;

        total_read += (size_t)bytes;
    }

    if (total_read < PACKET_REQUEST_SIZE)
        return 0;

    return 1;
}


int requestScheduler(struct request *req)
{
    // NEW: Prevent out-of-bounds indexing.

    if (req->prio < 1 || req->prio > 16)
        return -1;

    uint8_t p = req->prio - 1;

    struct QueueNode *node =
        malloc(sizeof(struct QueueNode));

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

        struct QueueNode *node = q->head;

        struct request *req = node->data;

        q->head = node->next;

        if (q->head == NULL)
            q->tail = NULL;

        free(node);

        return req;
    }

    return NULL;
}


int isQueueEmpty(void)
{
    for (int p = 0; p < 16; p++)
    {
        if (queues[p].head != NULL)
            return 0;
    }

    return 1;
}


// CHANGED: Handle partial sends.

void sendAnswer(int client_fd, uint64_t answer)
{
    uint64_t answer_net = htobe64(answer);

    const uint8_t *buf =
        (const uint8_t *)&answer_net;

    size_t total_sent = 0;

    while (total_sent < sizeof(answer_net))
    {
        ssize_t bytes = send(
            client_fd,
            buf + total_sent,
            sizeof(answer_net) - total_sent,
            MSG_NOSIGNAL
        );

        if (bytes < 0 && errno == EINTR)
            continue;

        if (bytes <= 0)
            break;

        total_sent += (size_t)bytes;
    }

    close(client_fd);
    counter++;
    if (counter == 100){
        destroy_hashtable(&database);
    }
}


int push(Stack *stack, const uint8_t *package)
{
    if (stack->top == MAX_SIZE - 1)
        return -1;

    stack->top++;

    memcpy(
        stack->array[stack->top],
        package,
        SOLUTION_SIZE
    );

    return 0;
}


int pop(Stack *stack, uint8_t *package)
{
    if (stack->top == -1)
        return -1;

    memcpy(
        package,
        stack->array[stack->top],
        SOLUTION_SIZE
    );

    stack->top--;

    return 0;
}


bool isEmpty(Stack *stack)
{
    return stack->top == -1;
}


void sendCompletedAnswers(void)
{
    uint8_t package[SOLUTION_SIZE];

    while (1)
    {
        pthread_mutex_lock(&stack_mutex);

        int result = pop(&stack, package);

        pthread_mutex_unlock(&stack_mutex);

        if (result == -1)
            break;

        int client_fd;
        uint64_t answer;

        memcpy(
            &client_fd,
            package,
            sizeof(int)
        );

        memcpy(
            &answer,
            package + sizeof(int),
            sizeof(uint64_t)
        );

        sendAnswer(client_fd, answer);
    }
}