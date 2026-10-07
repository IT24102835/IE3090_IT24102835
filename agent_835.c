#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include <sys/stat.h> // NEW: Required for creating directories

#define PORT 9410
#define SID_TAG "SID:5382"
#define AUTH_TOKEN "OPS-2835"
#define STORAGE_DIR "storage_5382"

void *handle_client(void *client_socket) {
    int sock = *(int*)client_socket;
    free(client_socket);

    char buffer[2048];
    int bytes_read;
    int authenticated = 0; 

    // Create the personalized storage directory if it doesn't exist
    mkdir(STORAGE_DIR, 0777);

    printf("Thread started for new connection.\n");

    while ((bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        // For standard text commands, we need to know the original length before we strip newlines
        // in case binary file data was sent in the exact same network packet as a PUT command.
        int original_bytes_read = bytes_read;
        buffer[bytes_read] = '\0'; 
        
        // Find where the command string ends (at the newline)
        int cmd_len = strcspn(buffer, "\r\n");
        buffer[cmd_len] = '\0'; // Null-terminate the command string
        
        // Calculate where the payload (binary data) would start if it was piggybacked
        int payload_offset = cmd_len;
        if (payload_offset < original_bytes_read && buffer[payload_offset] == '\r') payload_offset++;
        if (payload_offset < original_bytes_read && buffer[payload_offset] == '\n') payload_offset++;

        printf("Received command: %s\n", buffer);

        char response[2048] = {0}; 

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
        // Enforce authentication for all subsequent commands
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
        // 3. EXEC Command
        else if (strncmp(buffer, "EXEC ", 5) == 0) {
            char *name = buffer + 5; 
            char cmd[256] = {0};

            if (strcmp(name, "DATE") == 0) strcpy(cmd, "date");
            else if (strcmp(name, "UPTIME") == 0) strcpy(cmd, "uptime");
            else if (strcmp(name, "DISKFREE") == 0) strcpy(cmd, "df -h");
            else if (strcmp(name, "HOSTNAME") == 0) strcpy(cmd, "hostname");
            else if (strcmp(name, "WHOAMI") == 0) strcpy(cmd, "whoami");

            if (cmd[0] == '\0') {
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
        // 4. LISTPROC Command
        else if (strcmp(buffer, "LISTPROC") == 0) {
            FILE *fp = popen("ps -e -o pid=,comm= | head -n 30", "r");
            if (fp == NULL) {
                sprintf(response, "ERR 500 PROCESS_ERROR %s", SID_TAG);
            } else {
                char procs[1500] = {0}; 
                char line[256];
                int first = 1;
                
                while (fgets(line, sizeof(line), fp) != NULL) {
                    line[strcspn(line, "\n")] = 0; 
                    char *trimmed = line;
                    while(*trimmed == ' ') trimmed++;
                    if (strlen(procs) + strlen(trimmed) + 3 < sizeof(procs)) {
                        if (!first) strcat(procs, ", ");
                        strcat(procs, trimmed);
                        first = 0;
                    }
                }
                pclose(fp);
                sprintf(response, "OK PROCS %s %s", procs, SID_TAG);
            }
        }
        // 5. PUT Command (File Upload)
        else if (strncmp(buffer, "PUT ", 4) == 0) {
            char filename[256];
            int filesize;
            
            if (sscanf(buffer + 4, "%255s %d", filename, &filesize) == 2) {
                // Max file size check (e.g., 50MB) to match ERR 004 condition
                if (filesize > 52428800) { 
                    sprintf(response, "ERR 004 FILE_TOO_LARGE %s", SID_TAG);
                } else {
                    char filepath[512];
                    snprintf(filepath, sizeof(filepath), "%s/%s", STORAGE_DIR, filename);
                    
                    FILE *fp = fopen(filepath, "wb");
                    if (fp == NULL) {
                        sprintf(response, "ERR 500 CANNOT_CREATE_FILE %s", SID_TAG);
                    } else {
                        int remaining = filesize;
                        
                        // Write any binary data that arrived in the initial recv() buffer
                        int piggybacked_data = original_bytes_read - payload_offset;
                        if (piggybacked_data > 0) {
                            int to_write = (piggybacked_data < remaining) ? piggybacked_data : remaining;
                            fwrite(buffer + payload_offset, 1, to_write, fp);
                            remaining -= to_write;
                        }

                        // Loop to receive the rest of the binary data
                        char file_buf[4096];
                        while (remaining > 0) {
                            int bytes_to_read = (remaining < sizeof(file_buf)) ? remaining : sizeof(file_buf);
                            int received = recv(sock, file_buf, bytes_to_read, 0);
                            if (received <= 0) break; // Network error
                            
                            fwrite(file_buf, 1, received, fp);
                            remaining -= received;
                        }
                        
                        fclose(fp);
                        
                        if (remaining == 0) {
                            sprintf(response, "OK FILE_RECEIVED %s %s", filename, SID_TAG);
                        } else {
                            sprintf(response, "ERR 500 TRANSFER_FAILED %s", SID_TAG);
                        }
                    }
                }
            } else {
                sprintf(response, "ERR 400 INVALID_PUT_FORMAT %s", SID_TAG);
            }
        }
        // Unknown Command
        else {
            sprintf(response, "ERR 404 UNKNOWN_COMMAND %s", SID_TAG);
        }

        // Send the final response to the Controller
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
