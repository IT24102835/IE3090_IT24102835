#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/sysinfo.h> // NEW: For RAM and Uptime calculations

#define PORT 9410
#define SID_TAG "SID:5382"
#define AUTH_TOKEN "OPS-2835"

void *handle_client(void *client_socket) {
    int sock = *(int*)client_socket;
    free(client_socket);

    char buffer[1024];
    int bytes_read;
    int authenticated = 0; 

    printf("Thread started for new connection.\n");

    while ((bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_read] = '\0'; 
        buffer[strcspn(buffer, "\r\n")] = 0; 
        printf("Received command: %s\n", buffer);

        char response[2048] = {0}; // Increased size for command outputs

        // 1. AUTH Check
        if (strncmp(buffer, "AUTH ", 5) == 0) {
            char *token = buffer + 5; 
            if (strcmp(token, AUTH_TOKEN) == 0) {
                authenticated = 1;
                sprintf(response, "OK AUTHENTICATED %s", SID_TAG);
            } else {
                sprintf(response, "ERR 001 AUTH_FAILED %s", SID_TAG);
            }
        } 
        // Enforce authentication
        else if (!authenticated) {
            sprintf(response, "ERR 001 AUTH_FAILED %s", SID_TAG);
        }
        // 2. SYSINFO Command
        else if (strcmp(buffer, "SYSINFO") == 0) {
            struct sysinfo info;
            double load[1];
            if (sysinfo(&info) == 0 && getloadavg(load, 1) != -1) {
                unsigned long mem_used_mb = ((info.totalram - info.freeram) * info.mem_unit) / (1024 * 1024);
                sprintf(response, "OK SYSINFO %.2f %lu %ld %s", load[0], mem_used_mb, info.uptime, SID_TAG);
            } else {
                sprintf(response, "ERR 500 INTERNAL_ERROR %s", SID_TAG);
            }
        }
        // 3. EXEC Command (Whitelisted)
        else if (strncmp(buffer, "EXEC ", 5) == 0) {
            char *name = buffer + 5; 
            char cmd[256] = {0};

            // Map the whitelisted keywords to actual Linux commands
            if (strcmp(name, "DATE") == 0) strcpy(cmd, "date");
            else if (strcmp(name, "UPTIME") == 0) strcpy(cmd, "uptime");
            else if (strcmp(name, "DISKFREE") == 0) strcpy(cmd, "df -h");
            else if (strcmp(name, "HOSTNAME") == 0) strcpy(cmd, "hostname");
            else if (strcmp(name, "WHOAMI") == 0) strcpy(cmd, "whoami");

            if (cmd[0] == '\0') {
                // Command was not in the whitelist
                sprintf(response, "ERR 002 COMMAND_NOT_ALLOWED %s", SID_TAG);
            } else {
                FILE *fp = popen(cmd, "r");
                if (fp != NULL) {
                    char output[1024] = {0};
                    char line[256];
                    while (fgets(line, sizeof(line), fp) != NULL) {
                        if (strlen(output) + strlen(line) < sizeof(output) - 1) {
                            strcat(output, line);
                        }
                    }
                    pclose(fp);
                    sprintf(response, "OK EXEC_RESULT\n%s%s", output, SID_TAG);
                } else {
                    sprintf(response, "ERR 500 EXEC_FAILED %s", SID_TAG);
                }
            }
        }
        else {
            sprintf(response, "ERR 404 UNKNOWN_COMMAND %s", SID_TAG);
        }

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

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Agent started. Listening on port %d...\n", PORT);

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        int *new_sock = malloc(sizeof(int));
        *new_sock = new_socket;
        pthread_t thread_id;
        
        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) < 0) {
            perror("Could not create thread");
            free(new_sock);
            close(new_socket);
            continue;
        }
        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}
