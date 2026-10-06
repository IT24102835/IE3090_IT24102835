#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/utsname.h> 

#define PORT 9410
#define SID_TAG "SID:5382"
#define AUTH_TOKEN "OPS-2835"

void *handle_client(void *client_socket) {
    int sock = *(int*)client_socket;
    free(client_socket);

    char buffer[1024];
    int bytes_read;
    int authenticated = 0; // State variable to track authentication

    printf("Thread started for new connection.\n");

    while ((bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_read] = '\0'; 
        buffer[strcspn(buffer, "\r\n")] = 0; // Strip newline characters
        printf("Received command: %s\n", buffer);

        char response[1024] = {0};

        // 1. Check Authentication
        if (strncmp(buffer, "AUTH ", 5) == 0) {
            char *token = buffer + 5; 
            if (strcmp(token, AUTH_TOKEN) == 0) {
                authenticated = 1;
                strcpy(response, "200 OK: Authenticated successfully.");
            } else {
                strcpy(response, "401 Error: Invalid token.");
            }
        } 
        // 2. Enforce authentication for all other commands
        else if (!authenticated) {
            strcpy(response, "401 Error: Not authenticated. Please run AUTH first.");
        }
        // 3. SYSINFO Command
        else if (strcmp(buffer, "SYSINFO") == 0) {
            struct utsname sys_info;
            if (uname(&sys_info) == 0) {
                snprintf(response, sizeof(response), "200 OK: OS: %s, Release: %s, Machine: %s", 
                         sys_info.sysname, sys_info.release, sys_info.machine);
            } else {
                strcpy(response, "500 Error: Could not retrieve system info.");
            }
        }
        // 4. EXEC Command
        else if (strncmp(buffer, "EXEC ", 5) == 0) {
            char *cmd = buffer + 5; // Extract the command after "EXEC "
            FILE *fp = popen(cmd, "r");
            
            if (fp == NULL) {
                strcpy(response, "500 Error: Failed to execute command.");
            } else {
                char line[256];
                strcpy(response, "200 OK:\n"); // Start with success message
                
                // Read the output line by line
                while (fgets(line, sizeof(line), fp) != NULL) {
                    // Prevent buffer overflow (leave room for null terminator)
                    if (strlen(response) + strlen(line) < sizeof(response) - 1) {
                        strcat(response, line);
                    }
                }
                pclose(fp);
            }
        }
        // 5. Placeholder for future commands (PUT/GET, etc.)
        else {
            strcpy(response, "200 OK: Command received but not yet implemented.");
        }

        // Send the response back to the Controller
        send(sock, response, strlen(response), 0);
    }

    if (bytes_read == 0) {
        printf("Controller disconnected gracefully.\n");
    } else {
        perror("recv failed");
    }

    close(sock);
    return NULL;
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Attach socket to the port and reuse address
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind the socket to the network address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections
    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Agent started. Listening on port %d...\n", PORT);

    // Accept incoming connections in a loop
    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        // Allocate memory for the socket to pass safely to the thread
        int *new_sock = malloc(sizeof(int));
        *new_sock = new_socket;
        pthread_t thread_id;
        
        // Create a new thread for the connected client
        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) < 0) {
            perror("Could not create thread");
            free(new_sock);
            close(new_socket);
            continue;
        }
        
        // Detach the thread so its resources are freed when it finishes
        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}
