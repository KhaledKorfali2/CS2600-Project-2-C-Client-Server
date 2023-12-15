#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define LENGTH 2048

static _Atomic unsigned int cli_count = 0;
static int uid = 10;
int chat_history_fd;  // File descriptor for chat history

/* Client structure */
typedef struct {
    struct sockaddr_in address;
    int sockfd;
    int uid;
    char name[32];
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void str_trim_lf(char *arr, int length) {
    int i;
    for (i = 0; i < length; i++) { // trim \n
        if (arr[i] == '\n') {
            arr[i] = '\0';
            break;
        }
    }
}

/* Add clients to queue */
void queue_add(client_t *cl) {
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i]) {
            clients[i] = cl;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

/* Remove clients from queue */
void queue_remove(int uid) {
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i]) {
            if (clients[i]->uid == uid) {
                clients[i] = NULL;
                break;
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

/* Send message to all clients except sender */
void send_message(char *s, int uid, char *name) {
    pthread_mutex_lock(&clients_mutex);

    // Check if it's a server message (join/leave)
    int is_server_message = (strcmp(name, "Server") == 0);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i]) {
            char message[BUFFER_SZ + 32];
            // Format the message based on whether it's a server message or not
            if (is_server_message) {
                sprintf(message, "Server: %s", s);
            } else {
                // Identify the sender client and format the message accordingly
                if (clients[i]->uid == uid) {
                    sprintf(message, "You: %s", s);
                } else {
                    sprintf(message, "%s: %s", name, s);
                }
            }

            if (write(clients[i]->sockfd, message, strlen(message)) < 0) {
                perror("ERROR: write to descriptor failed");
                break;
            }
        }
    }

    // Write the original message to chat history file
    if (!is_server_message) {
        if (write(chat_history_fd, name, strlen(name)) < 0) {
            perror("ERROR: write to chat history file failed");
        }
        if (write(chat_history_fd, ": ", 2) < 0) {
            perror("ERROR: write to chat history file failed");
        }
        if (write(chat_history_fd, s, strlen(s)) < 0) {
            perror("ERROR: write to chat history file failed");
        }
        if (write(chat_history_fd, "\n", 1) < 0) {
            perror("ERROR: write to chat history file failed");
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

/* Handle all communication with the client */
void *handle_client(void *arg) {
    char buff_out[BUFFER_SZ];
    char name[32];
    int leave_flag = 0;

    cli_count++;
    client_t *cli = (client_t *)arg;

    // Name
    if (recv(cli->sockfd, name, 32, 0) <= 0 || strlen(name) < 2 || strlen(name) >= 32 - 1) {
        printf("Didn't enter the name.\n");
        leave_flag = 1;
    } else {
        strcpy(cli->name, name);
        sprintf(buff_out, "%s has joined\n", cli->name);
        printf("%s", buff_out);
        send_message(buff_out, cli->uid, "Server");
    }

    bzero(buff_out, BUFFER_SZ);

    while (1) {
        if (leave_flag) {
            break;
        }

        int receive = recv(cli->sockfd, buff_out, BUFFER_SZ, 0);
        if (receive > 0) {
            if (strlen(buff_out) > 0) {
                str_trim_lf(buff_out, strlen(buff_out));

                // Display the message with "You:" prefix for the sending client
                if (cli->uid == uid) {
                    printf("> You: %s\n", buff_out);
                    send_message(buff_out, cli->uid, cli->name);
                } else {
                    send_message(buff_out, cli->uid, cli->name);
                    printf("%s -> %s: %s\n", cli->name, (cli->uid == uid) ? "You" : cli->name, buff_out);
                }
            }
        } else if (receive == 0 || strcmp(buff_out, "exit") == 0) {
            leave_flag = 1;
        } else {
            printf("ERROR: -1\n");
            leave_flag = 1;
        }

        bzero(buff_out, BUFFER_SZ);
    }

    /* Notify that the client has left */
    sprintf(buff_out, "%s has left\n", cli->name);
    printf("%s", buff_out);
    send_message(buff_out, cli->uid, "Server");

    /* Delete client from the queue and yield thread */
    close(cli->sockfd);
    queue_remove(cli->uid);
    free(cli);
    cli_count--;
    pthread_detach(pthread_self());

    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *ip = "127.0.0.1";
    int port = atoi(argv[1]);
    int option = 1;
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;

    /* Socket settings */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    serv_addr.sin_port = htons(port);

    /* Ignore pipe signals */
    signal(SIGPIPE, SIG_IGN);

    if (setsockopt(listenfd, SOL_SOCKET, (SO_REUSEPORT | SO_REUSEADDR), (char *)&option, sizeof(option)) < 0) {
        perror("ERROR: setsockopt failed");
        return EXIT_FAILURE;
    }

    /* Bind */
    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR: Socket binding failed");
        return EXIT_FAILURE;
    }

    /* Listen */
    if (listen(listenfd, 10) < 0) {
        perror("ERROR: Socket listening failed");
        return EXIT_FAILURE;
    }

    // Open the chat history file or create it if it doesn't exist
    chat_history_fd = open("chat_history", O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
    if (chat_history_fd == -1) {
        perror("ERROR: Unable to open chat_history file");
        return EXIT_FAILURE;
    }

    printf("=== WELCOME TO THE CHATROOM ===\n");

    while (1) {
        socklen_t clilen = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);

        /* Check if max clients are reached */
        if ((cli_count + 1) == MAX_CLIENTS) {
            printf("Max clients reached. Rejected.\n");
            close(connfd);
            continue;
        }

        /* Client settings */
        client_t *cli = (client_t *)malloc(sizeof(client_t));
        cli->address = cli_addr;
        cli->sockfd = connfd;
        cli->uid = uid++;

        /* Add client to the queue and fork thread */
        queue_add(cli);
        pthread_create(&tid, NULL, &handle_client, (void *)cli);

        /* Reduce CPU usage */
        sleep(1);
    }

    // Close the chat history file
    close(chat_history_fd);

    return EXIT_SUCCESS;
}

