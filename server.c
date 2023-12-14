#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define RESPONSE_SIZE 4096
#define HISTORY_FILE "chat_history"

pthread_t tid[MAX_CLIENTS];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct ThreadArgs {
    int client_socket;
};

void send_welcome_message(int client_socket, const char *username) {
    char welcome_message[BUFFER_SIZE];
    snprintf(welcome_message, sizeof(welcome_message), "Welcome, %s!\n", username);
    send(client_socket, welcome_message, strlen(welcome_message), 0);
}

void broadcast_join_message(const char *username, int new_client_socket) {
    char join_message[BUFFER_SIZE];
    snprintf(join_message, sizeof(join_message), "Server: %s has joined the chat\n", username);

    FILE *history_file = fopen(HISTORY_FILE, "a");
    if (history_file == NULL) {
        perror("Failed to open chat history file");
        exit(EXIT_FAILURE);
    }

    fprintf(history_file, "%s", join_message);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (tid[i] != 0 && tid[i] != pthread_self() && tid[i] != new_client_socket) {
            struct ThreadArgs *threadArgs = (struct ThreadArgs *)tid[i];
            send(threadArgs->client_socket, join_message, strlen(join_message), 0);
        }
    }

    fclose(history_file);
}

void broadcast_leave_message(const char *username) {
    char leave_message[BUFFER_SIZE];
    snprintf(leave_message, sizeof(leave_message), "Server: %s has left the chat\n", username);

    FILE *history_file = fopen(HISTORY_FILE, "a");
    if (history_file == NULL) {
        perror("Failed to open chat history file");
        exit(EXIT_FAILURE);
    }

    fprintf(history_file, "%s", leave_message);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (tid[i] != 0 && tid[i] != pthread_self()) {
            struct ThreadArgs *threadArgs = (struct ThreadArgs *)tid[i];
            send(threadArgs->client_socket, leave_message, strlen(leave_message), 0);
        }
    }

    fclose(history_file);
}

void *handle_client(void *arg) {
    struct ThreadArgs *threadArgs = (struct ThreadArgs *)arg;
    int client_socket = threadArgs->client_socket;
    char buffer[BUFFER_SIZE];
    char username[BUFFER_SIZE];

    ssize_t username_length = recv(client_socket, username, sizeof(username), 0);
    if (username_length <= 0) {
        close(client_socket);
        free(threadArgs);
        pthread_exit(NULL);
    }

    username[username_length] = '\0';

    pthread_mutex_lock(&mutex);
    broadcast_join_message(username, client_socket);
    send_welcome_message(client_socket, username);
    pthread_mutex_unlock(&mutex);

    while (1) {
        ssize_t received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            break;
        }

        buffer[received] = '\0';

        FILE *history_file = fopen(HISTORY_FILE, "a");
        if (history_file == NULL) {
            perror("Failed to open chat history file");
            exit(EXIT_FAILURE);
        }

        fprintf(history_file, "%s said: %s\n", username, buffer);

        char response[RESPONSE_SIZE];
        snprintf(response, sizeof(response), "%s said: %s", username, buffer);
        send(client_socket, response, strlen(response), 0);

        fclose(history_file);
    }

    pthread_mutex_lock(&mutex);
    broadcast_leave_message(username);
    pthread_mutex_unlock(&mutex);

    close(client_socket);
    free(threadArgs);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    int server_socket, client_socket;
    struct sockaddr_in server_address, client_address;
    socklen_t address_len = sizeof(client_address);

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        perror("Binding failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, MAX_CLIENTS) == -1) {
        perror("Listening failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    int i = 0;
    while (1) {
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_address, &address_len)) == -1) {
            perror("Acceptance failed");
            exit(EXIT_FAILURE);
        }

        struct ThreadArgs *threadArgs = (struct ThreadArgs *)malloc(sizeof(struct ThreadArgs));
        if (threadArgs == NULL) {
            perror("Thread arguments allocation failed");
            exit(EXIT_FAILURE);
        }
        threadArgs->client_socket = client_socket;

        if (pthread_create(&tid[i], NULL, handle_client, (void *)threadArgs) != 0) {
            perror("Thread creation failed");
            exit(EXIT_FAILURE);
        }

        i = (i + 1) % MAX_CLIENTS; // Circular buffer
    }

    close(server_socket);
    return 0;
}

