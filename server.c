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

    if (client_fd == -1) {
        perror("accept");
        continue;
    }

    printf("Client connected!\n");

    char buffer[1024];

    while (1) {

        ssize_t bytes_received =
            recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received < 0) {
            perror("recv");
            break;
        }

        if (bytes_received == 0) {
            printf("Client disconnected\n");
            break;
        }

        buffer[bytes_received] = '\0';

        printf("Received %zd bytes: %s\n",
               bytes_received,
               buffer);

        // Her skal du sandsynligvis sende et svar tilbage

        send(client_fd, buffer, bytes_received, 0);
    }

    close(client_fd);

    close(server_fd);

    //(Provisional name)

    // Client might use 5003 as port number

    // inputStore(input);

    // inputHandler(inputStore.getInput());

    // decoder(inputHandler.getHas(), inputHandler.getStart(), inputHandler.getEnd());

    // sender(decoder.getAnswer());

    // while(1){
    //     //Program probably runs inside here

    // }

    return 0;
}
