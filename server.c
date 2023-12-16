#include <sys/socket.h> // Socket programming functions
#include <netinet/in.h> // Internet address family functions
#include <arpa/inet.h>  // Functions for manipulating IP addresses
#include <stdio.h>      // Standard Input and Output functions
#include <stdlib.h>     // Standard Library functions
#include <unistd.h>     // POSIX operating system API
#include <errno.h>      // Error Handling
#include <string.h>     // String manipulation functions
#include <pthread.h>    // POSIX threads for multithreading
#include <sys/types.h>  // Data types used in system calls
#include <signal.h>     // Signal handling functions
#include <fcntl.h>      // File control options

// Pre-processors and constants
#define MAX_CLIENTS 100 // Maximum number of clients the server can handle
#define BUFFER_SZ 2048  // Maximum buffer size for messages
#define LENGTH 2048     // Maximum length for various strings


static _Atomic unsigned int cli_count = 0;  // Atomic variable to keep track of the number of connected clients
static int uid = 10;                        // User ID counter, starting from 10 to avoid conflicts with system IDs
int chat_history_fd;                        // File descriptor for chat history

/* Client structure */
typedef struct {
    struct sockaddr_in address;
    int sockfd;
    int uid;
    char name[32];
} client_t;

client_t *clients[MAX_CLIENTS];             // Array to store client information structures (client_t) for all connected clients

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;  // Mutex to control access to the clients array to prevent data race conditions


// Function to trim trailing newline character from a string
void str_trim_lf(char *arr, int length) {
    int i;
    for (i = 0; i < length; i++) { // Iterate through the characters in the array
        if (arr[i] == '\n') {     // Check if the current character is a newline character
            arr[i] = '\0';        // Replace the newline character with null terminator
            break;                // Break out of the loop after trimming the newline
        }
    }
}

// Function to add a client to the array of connected clients
void queue_add(client_t *cl) {
    // Lock the mutex to ensure exclusive access to the clients array
    pthread_mutex_lock(&clients_mutex);

    // Iterate through the clients array to find an available slot
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        // Check if the current slot is empty (NULL)
        if (!clients[i]) {
            // Assign the client structure to the empty slot
            clients[i] = cl;
            // Break out of the loop after adding the client
            break;
        }
    }

    // Unlock the mutex to allow other threads to access the clients array
    pthread_mutex_unlock(&clients_mutex);
}

// Function to remove a client from the array of connected clients based on UID
void queue_remove(int uid) {
    // Lock the mutex to ensure exclusive access to the clients array
    pthread_mutex_lock(&clients_mutex);

    // Iterate through the clients array to find the client with the specified UID
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        // Check if the current slot is not empty (non-NULL)
        if (clients[i]) {
            // Check if the UID of the current client matches the specified UID
            if (clients[i]->uid == uid) {
                // Set the slot to NULL to remove the client from the array
                clients[i] = NULL;
                // Break out of the loop after removing the client
                break;
            }
        }
    }

    // Unlock the mutex to allow other threads to access the clients array
    pthread_mutex_unlock(&clients_mutex);
}

// Function to send a message to all clients except the sender
void send_message(char *s, int uid, char *name) {
    // Lock the mutex to ensure exclusive access to the clients array
    pthread_mutex_lock(&clients_mutex);

    // Check if it's a server message (join/leave)
    int is_server_message = (strcmp(name, "Server") == 0);
    
    // Iterate through the clients array to send the message to each connected client
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

    // Unlock the mutex to allow other threads to access the clients array
    pthread_mutex_unlock(&clients_mutex);
}


// Thread function to handle communication with a client
void *handle_client(void *arg) {
    char buff_out[BUFFER_SZ];
    char name[32];
    int leave_flag = 0;

    cli_count++; // Increment the count of connected clients

    client_t *cli = (client_t *)arg; // Cast the argument to a client structure
    
    
    // Get the client's name
    if (recv(cli->sockfd, name, 32, 0) <= 0 || strlen(name) < 2 || strlen(name) >= 32 - 1) {
        printf("Didn't enter the name.\n");
        leave_flag = 1;
    } else {
        // Assign the client's name and send a join message to all clients
        strcpy(cli->name, name);
        sprintf(buff_out, "%s has joined\n", cli->name);
        printf("%s", buff_out);
        send_message(buff_out, cli->uid, "Server");


        // Write the join message to chat history
        if (write(chat_history_fd, "Server: ", 9) < 0) {
            perror("ERROR: write to chat history file failed");
        }
        if (write(chat_history_fd, cli->name, strlen(cli->name)) < 0) {
            perror("ERROR: write to chat history file failed");
        }
        if (write(chat_history_fd, " has joined\n", 12) < 0) {
            perror("ERROR: write to chat history file failed");
        }
    }
    // Clear the output buffer
    bzero(buff_out, BUFFER_SZ);

    while (1) {
        // Check if the client wants to leave
        if (leave_flag) {
            break;
        }

        // Receive a message from the client
        int receive = recv(cli->sockfd, buff_out, BUFFER_SZ, 0);
        if (receive > 0) {
             // Check if the received message is not empty
            if (strlen(buff_out) > 0) {
                // Trim the trailing newline character from the message
                str_trim_lf(buff_out, strlen(buff_out));

                // Display the message with "You:" prefix for the sending client
                if (cli->uid == uid) {
                    send_message(buff_out, cli->uid, cli->name);
                } else {
                    // Display the message with sender information for other clients
                    printf("%s -> %s: %s\n", cli->name, (cli->uid == uid) ? "You" : cli->name, buff_out);
                    send_message(buff_out, cli->uid, cli->name);
                }
            }
        } else if (receive == 0 || strcmp(buff_out, "exit") == 0) {
            // If the client disconnected or sent an exit command, set the leave flag
            leave_flag = 1;
        } else {
            // Handle the case when receive returns -1 (error)
            printf("ERROR: -1\n");
            leave_flag = 1;
        }

        // Clear the output buffer
        bzero(buff_out, BUFFER_SZ);
    }

    /* Notify that the client has left */
    sprintf(buff_out, "%s has left\n", cli->name);
    printf("%s", buff_out);
    send_message(buff_out, cli->uid, "Server");

    // Write the leave message to chat history
    if (write(chat_history_fd, "Server: ", 9) < 0) {
        perror("ERROR: write to chat history file failed");
    }
    if (write(chat_history_fd, cli->name, strlen(cli->name)) < 0) {
        perror("ERROR: write to chat history file failed");
    }
    if (write(chat_history_fd, " has left\n", 10) < 0) {
        perror("ERROR: write to chat history file failed");
    }

    /* Delete client from the queue and yield thread */
    close(cli->sockfd);
    queue_remove(cli->uid);
    free(cli);
    cli_count--;

    // Detach the thread
    pthread_detach(pthread_self());
    
    // Return NULL to indicate the thread's completion
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
