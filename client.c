#include <stdio.h>      // Standard Input and Output functions
#include <stdlib.h>     // Standard Library functions
#include <string.h>     // String manipulation functions
#include <signal.h>     // Signal handling functions
#include <unistd.h>     // POSIX operating system API
#include <sys/types.h>  // Data types used in system calls
#include <sys/socket.h> // Socket programming functions
#include <netinet/in.h> // Internet address family functions
#include <arpa/inet.h>  // Functions for manipulating IP addresses
#include <pthread.h>    // POSIX threads for multithreading
#include <fcntl.h>      // File control options

#define LENGTH 2048     // Max message length

// Global variables
volatile sig_atomic_t flag = 0;  // Signal flag for graceful exit
int sockfd = 0;                   // Socket file descriptor
char name[32];                    // User's name
int chat_history_fd;              // File descriptor for chat history

// Function to overwrite stdout for a cleaner console output
void str_overwrite_stdout() {
    // Move the cursor to the beginning of the line and print the prompt
    printf("\r%s", "> ");
    // Flush the standard output stream to ensure the prompt is immediately displayed
    fflush(stdout);
}

// Function to trim trailing newline character from a string
void str_trim_lf(char *arr, int length) {
    int i;
    for (i = 0; i < length; i++) { // Iterate through the characters in the array
        if (arr[i] == '\n') {       // Check if the current character is a newline character
            arr[i] = '\0';          // Replace the newline character with null terminator
            break;                  // Break out of the loop after trimming the newline
        }
    }
}

// Signal handler for Ctrl+C to gracefully exit the program
void catch_ctrl_c_and_exit(int sig) {
    flag = 1;
}

// Thread function to handle receiving messages from the server
void recv_msg_handler() {
    char message[LENGTH] = {};
    
    // Continuous loop to receive messages
    while (1) {
        // Receive a message from the server
        int receive = recv(sockfd, message, LENGTH, 0);
        if (receive > 0) {
            // Check if the message is not from the current client
            if (strncmp(message, name, strlen(name)) != 0) {
                printf("%s\n", message);    // Print the received message to the console
            }
            str_overwrite_stdout();         // Update the console prompt
        } else if (receive == 0) {
            // Connection closed by the server, break out of loop
            break;
        } else {
            // Handle the case when recv returns -1 (error)
            perror("ERROR: receive");
        }
        // Clear the message buffer
        memset(message, 0, sizeof(message));
    }
}

// Thread function to handle sending messages to the server
void send_msg_handler() {
    char message[LENGTH] = {};
    while (1) {
        // Overwrite the current line in the console to provide a clean input prompt
        str_overwrite_stdout();

        // Read user input from the standard input (keyboard)
        fgets(message, LENGTH, stdin);

        // Trim the trailing newline character from the input message
        str_trim_lf(message, LENGTH);

        // Check if the user wants to exit
        if (strcmp(message, "exit") == 0) {
            break;
        } else {
            // Send the message to the server
            send(sockfd, message, strlen(message), 0);

            // Clear the input line on the sender's terminal
            printf("\033[A\033[K");
        }

        // Clear the message buffer
        bzero(message, LENGTH);
    }
    // Notify the main thread to exit gracefully
    catch_ctrl_c_and_exit(2);
}

// Main function
int main(int argc, char **argv) {
    // Check if the correct number of command-line arguments is provided
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *ip = "127.0.0.1";
    int port = atoi(argv[1]);

    // Register Ctrl+C signal handler
    signal(SIGINT, catch_ctrl_c_and_exit);

    // Prompt user to enter their name
    printf("Please enter your name: ");
    fgets(name, 32, stdin);
    str_trim_lf(name, strlen(name));

    // Validate the length of the user's name
    if (strlen(name) > 32 || strlen(name) < 2) {
        printf("Name must be less than 30 and more than 2 characters.\n");
        return EXIT_FAILURE;
    }

    // Open the chat history file or create it if it doesn't exist
    chat_history_fd = open("chat_history", O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
    if (chat_history_fd == -1) {
        perror("ERROR: Unable to open chat_history file");
        return EXIT_FAILURE;
    }

    // Set up server address structure
    struct sockaddr_in server_addr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    // Connect to the server
    int err = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err == -1) {
        perror("ERROR: connect");
        return EXIT_FAILURE;
    }

    // Send the user's name to the server
    send(sockfd, name, 32, 0);

    // Display welcome message
    printf("=== WELCOME TO THE CHATROOM ===\n");

    // Create threads for sending and receiving messages
    pthread_t send_msg_thread;
    if (pthread_create(&send_msg_thread, NULL, (void *)send_msg_handler, NULL) != 0) {
        perror("ERROR: pthread");
        return EXIT_FAILURE;
    }

    pthread_t recv_msg_thread;
    if (pthread_create(&recv_msg_thread, NULL, (void *)recv_msg_handler, NULL) != 0) {
        perror("ERROR: pthread");
        return EXIT_FAILURE;
    }

    // Main loop to wait for the exit signal
    while (1) {
        if (flag) {
            // Display a goodbye message
            printf("\nBye\n");
            break;
        }
    }

    // Close the socket and chat history file
    close(sockfd);
    close(chat_history_fd);

    return EXIT_SUCCESS;
}

