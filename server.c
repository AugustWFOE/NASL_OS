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

        printf("Client connected!\n");

        uint8_t req_buf[PACKET_REQUEST_SIZE];
        
        printf("1\n");

        ssize_t total_read = 0;
        while (total_read < PACKET_REQUEST_SIZE) {
            ssize_t bytes = read(client_fd, req_buf + total_read, PACKET_REQUEST_SIZE - total_read);
            if (bytes <= 0) {
                break;
            }
            total_read += bytes;
            printf("2\n");
        }
         printf("out read\n");
        if (total_read < PACKET_REQUEST_SIZE) {
            printf("3 error\n");
            close(client_fd);
            continue;
        }

        uint8_t *target_hash = &req_buf[PACKET_REQUEST_HASH_OFFSET];

        uint64_t start_net, end_net;
        memcpy(&start_net, &req_buf[PACKET_REQUEST_START_OFFSET], sizeof(uint64_t));
        memcpy(&end_net, &req_buf[PACKET_REQUEST_END_OFFSET], sizeof(uint64_t));
        uint64_t start = be64toh(start_net);
        uint64_t end = be64toh(end_net);

        uint8_t priority = req_buf[PACKET_REQUEST_PRIO_OFFSET];

        uint64_t answer=0;
        uint8_t hash[32]; 
        printf("4 val split\n");
        for (uint64_t i = start; i <= end; i++) {
            uint64_t le_val = htole64(i);

            lonesha256(hash, (const unsigned char *)&le_val, sizeof(le_val));
            printf("5 finding hash\n");
            if (memcmp(hash, target_hash, 32) == 0) {
                answer = i;
                printf("6 hit\n");
                break;
            }
        }

        uint64_t answer_net = htobe64(answer);
        printf("7 write answer\n");
        write(client_fd, &answer_net, PACKET_RESPONSE_SIZE);

        printf("[server] %llu\n", (unsigned long long)answer);

        // pthread_create() goes here

        close(client_fd);

        // Client might use 5003 as port number

        // inputStore(input);

        // inputHandler(inputStore.getInput());

        // decoder(inputHandler.getHas(), inputHandler.getStart(), inputHandler.getEnd());

        // sender(decoder.getAnswer());

        // while(1){
        //     //Program probably runs inside here

        // }


    }

    close(server_fd);
    return 0;
}
