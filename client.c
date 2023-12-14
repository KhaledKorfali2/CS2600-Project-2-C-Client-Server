#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 2048
#define RESPONSE_SIZE 4096

int main() {
    int client_socket;
    struct sockaddr_in server_address;
    char buffer[BUFFER_SIZE];
    char response[RESPONSE_SIZE];

    if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    if (connect(client_socket, (struct sockaddr*)&server_address, sizeof(server_address)) == -1) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    printf("Enter your username: ");
    fgets(buffer, sizeof(buffer), stdin);
    buffer[strcspn(buffer, "\n")] = '\0';

    send(client_socket, buffer, strlen(buffer), 0);

    ssize_t received = recv(client_socket, buffer, sizeof(buffer), 0);
    if (received <= 0) {
        perror("Error receiving welcome message");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server says: %.*s\n", (int)received, buffer);

    // Start chat loop
    while (1) {
        // Prompt user to enter text
        printf("Enter your message (type 'quit' to exit): ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = '\0'; // Remove trailing newline

        // Send user's message to the server
        send(client_socket, buffer, strlen(buffer), 0);

        // Check if the user wants to quit
        if (strcmp(buffer, "quit") == 0) {
            break;
        }

        // Receive and print the server's response
        received = recv(client_socket, response, sizeof(response), 0);
        if (received <= 0) {
            perror("Error receiving server response");
            break; // Server disconnected
        }

        printf("Server says: %.*s\n", (int)received, response);
    }

    close(client_socket);
    return 0;
}

