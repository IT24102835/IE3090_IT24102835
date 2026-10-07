#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <time.h> 

#define PORT 9410
#define SID_TAG "SID:5382"
#define AUTH_TOKEN "OPS-2835"
#define STORAGE_DIR "./agentfiles/IT24102835" 
#define LOG_FILE "remoteops_IT24102835.log"   

void log_event(const char *message) {
    FILE *fp = fopen(LOG_FILE, "a");
    if (fp != NULL) {
        time_t now = time(NULL);
        char *dt = ctime(&now);
        dt[strlen(dt)-1] = '\0'; 
        fprintf(fp, "[%s] %s\n", dt, message);
        fclose(fp);
    }
}

typedef struct {
    char ip[INET_ADDRSTRLEN];
    int udp_port;
    volatile int *active_flag;
} monitor_args_t;

void *udp_monitor_thread(void *args) {
    monitor_args_t *m_args = (monitor_args_t *)args;
    int sockfd;
    struct sockaddr_in dest_addr;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        free(m_args);
        return NULL;
    }

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(m_args->udp_port);
    inet_pton(AF_INET, m_args->ip, &dest_addr.sin_addr);

    while (*(m_args->active_flag)) {
        struct sysinfo info;
        double load[1];
        char payload[256];
        
        if (sysinfo(&info) == 0 && getloadavg(load, 1) != -1) {
            unsigned long mem_used_mb = ((info.totalram - info.freeram) * info.mem_unit) / (1024 * 1024);
            snprintf(payload, sizeof(payload), "OK SYSINFO %.2f %lu %ld %s", 
                     load[0], mem_used_mb, info.uptime, SID_TAG);
            sendto(sockfd, payload, strlen(payload), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        }
        
        for(int i = 0; i < 20 && *(m_args->active_flag); i++) usleep(100000); 
    }

    close(sockfd);
    free(m_args);
    return NULL;
}

void *handle_client(void *client_socket) {
    int sock = *(int*)client_socket;
    free(client_socket);

    char buffer[2048];
    int bytes_read;
    int authenticated = 0; 
    
    volatile int monitor_active = 0;
    pthread_t monitor_tid = 0;
    int monitor_running = 0;

    mkdir("./agentfiles", 0777);
    mkdir(STORAGE_DIR, 0777);
    
    printf("Thread started for new connection.\n");
    log_event("New client connected.");

    while ((bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        int original_bytes_read = bytes_read;
        buffer[bytes_read] = '\0'; 
        
        int cmd_len = strcspn(buffer, "\r\n");
        
        // FIXED: Calculate the exact byte offset before truncating the string!
        int payload_offset = cmd_len;
        if (buffer[payload_offset] == '\r') payload_offset++;
        if (buffer[payload_offset] == '\n') payload_offset++;
        
        buffer[cmd_len] = '\0'; 

        if (strlen(buffer) > 0) {
            printf("Received command: %s\n", buffer);
            char log_msg[2100]; 
            snprintf(log_msg, sizeof(log_msg), "Received command: %s", buffer);
            log_event(log_msg);
        }

        char response[2048] = {0}; 

        if (strncmp(buffer, "AUTH ", 5) == 0) {
            char *token = buffer + 5; 
            if (strcmp(token, AUTH_TOKEN) == 0) {
                authenticated = 1;
                sprintf(response, "OK AUTHENTICATED %s", SID_TAG);
                log_event("Authentication successful.");
            } else {
                sprintf(response, "ERR 001 AUTH_FAILED %s", SID_TAG);
                log_event("Authentication failed.");
            }
        } 
        else if (!authenticated) {
            sprintf(response, "ERR 001 AUTH_FAILED %s", SID_TAG);
        }
        else if (strcmp(buffer, "SYSINFO") == 0) {
            struct sysinfo info;
            double load[1];
            if (sysinfo(&info) == 0 && getloadavg(load, 1) != -1) {
                unsigned long mem_used_mb = ((info.totalram - info.freeram) * info.mem_unit) / (1024 * 1024);
                sprintf(response, "OK SYSINFO %.2f %lu %ld %s", load[0], mem_used_mb, info.uptime, SID_TAG);
            } else sprintf(response, "ERR 500 INTERNAL_ERROR %s", SID_TAG);
        }
        else if (strncmp(buffer, "EXEC ", 5) == 0) {
            char *name = buffer + 5; 
            char cmd[256] = {0};

            if (strcmp(name, "DATE") == 0) strcpy(cmd, "date");
            else if (strcmp(name, "UPTIME") == 0) strcpy(cmd, "uptime");
            else if (strcmp(name, "DISKFREE") == 0) strcpy(cmd, "df -h");
            else if (strcmp(name, "HOSTNAME") == 0) strcpy(cmd, "hostname");
            else if (strcmp(name, "WHOAMI") == 0) strcpy(cmd, "whoami");

            if (cmd[0] == '\0') sprintf(response, "ERR 002 COMMAND_NOT_ALLOWED %s", SID_TAG);
            else {
                FILE *fp = popen(cmd, "r");
                if (fp != NULL) {
                    char output[1024] = {0};
                    char line[256];
                    while (fgets(line, sizeof(line), fp) != NULL) {
                        if (strlen(output) + strlen(line) < sizeof(output) - 1) strcat(output, line);
                    }
                    pclose(fp);
                    sprintf(response, "OK EXEC_RESULT\n%s%s", output, SID_TAG);
                } else sprintf(response, "ERR 500 EXEC_FAILED %s", SID_TAG);
            }
        }
        else if (strcmp(buffer, "LISTPROC") == 0) {
            FILE *fp = popen("ps -e -o pid=,comm= | head -n 30", "r");
            if (fp == NULL) sprintf(response, "ERR 500 PROCESS_ERROR %s", SID_TAG);
            else {
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
        else if (strncmp(buffer, "PUT ", 4) == 0) {
            char filename[256];
            int filesize;
            if (sscanf(buffer + 4, "%255s %d", filename, &filesize) == 2) {
                if (filesize > 52428800) sprintf(response, "ERR 004 FILE_TOO_LARGE %s", SID_TAG);
                else {
                    char filepath[512];
                    snprintf(filepath, sizeof(filepath), "%s/%s", STORAGE_DIR, filename);
                    FILE *fp = fopen(filepath, "wb");
                    if (fp == NULL) sprintf(response, "ERR 500 CANNOT_CREATE_FILE %s", SID_TAG);
                    else {
                        int remaining = filesize;
                        int piggybacked = original_bytes_read - payload_offset;
                        if (piggybacked > 0) {
                            int to_write = (piggybacked < remaining) ? piggybacked : remaining;
                            fwrite(buffer + payload_offset, 1, to_write, fp);
                            remaining -= to_write;
                        }
                        char file_buf[4096];
                        while (remaining > 0) {
                            int bytes_to_read = (remaining < sizeof(file_buf)) ? remaining : sizeof(file_buf);
                            int received = recv(sock, file_buf, bytes_to_read, 0);
                            if (received <= 0) break; 
                            fwrite(file_buf, 1, received, fp);
                            remaining -= received;
                        }
                        fclose(fp);
                        if (remaining == 0) {
                            sprintf(response, "OK FILE_RECEIVED %s %s", filename, SID_TAG);
                            char log_msg[512]; 
                            snprintf(log_msg, sizeof(log_msg), "File uploaded: %s", filename);
                            log_event(log_msg);
                        } else sprintf(response, "ERR 500 TRANSFER_FAILED %s", SID_TAG);
                    }
                }
            } else sprintf(response, "ERR 400 INVALID_PUT_FORMAT %s", SID_TAG);
        }
        else if (strncmp(buffer, "GET ", 4) == 0) {
            char filename[256];
            if (sscanf(buffer + 4, "%255s", filename) == 1) {
                char filepath[512];
                snprintf(filepath, sizeof(filepath), "%s/%s", STORAGE_DIR, filename);
                FILE *fp = fopen(filepath, "rb");
                if (fp == NULL) sprintf(response, "ERR 005 FILE_NOT_FOUND %s", SID_TAG);
                else {
                    fseek(fp, 0L, SEEK_END);
                    long filesize = ftell(fp);
                    rewind(fp);
                    char header[512];
                    snprintf(header, sizeof(header), "OK FILE_SEND %s %ld %s\r\n", filename, filesize, SID_TAG);
                    send(sock, header, strlen(header), 0);
                    char file_buf[4096];
                    size_t bytes_read;
                    while ((bytes_read = fread(file_buf, 1, sizeof(file_buf), fp)) > 0) {
                        send(sock, file_buf, bytes_read, 0);
                    }
                    fclose(fp);
                    
                    char log_msg[512]; 
                    snprintf(log_msg, sizeof(log_msg), "File downloaded: %s", filename);
                    log_event(log_msg);
                    continue; 
                }
            } else sprintf(response, "ERR 400 INVALID_GET_FORMAT %s", SID_TAG);
        }
        else if (strncmp(buffer, "MONITOR START ", 14) == 0) {
            int udp_port;
            if (sscanf(buffer + 14, "%d", &udp_port) == 1) {
                if (!monitor_active) {
                    struct sockaddr_in peer_addr;
                    socklen_t peer_len = sizeof(peer_addr);
                    getpeername(sock, (struct sockaddr*)&peer_addr, &peer_len); 
                    
                    monitor_args_t *m_args = malloc(sizeof(monitor_args_t));
                    inet_ntop(AF_INET, &peer_addr.sin_addr, m_args->ip, INET_ADDRSTRLEN);
                    m_args->udp_port = udp_port;
                    m_args->active_flag = &monitor_active;
                    
                    monitor_active = 1;
                    if (pthread_create(&monitor_tid, NULL, udp_monitor_thread, (void*)m_args) == 0) {
                        monitor_running = 1;
                        sprintf(response, "OK MONITOR_STARTED %s", SID_TAG);
                    } else {
                        monitor_active = 0;
                        free(m_args);
                        sprintf(response, "ERR 500 THREAD_ERROR %s", SID_TAG);
                    }
                } else sprintf(response, "ERR 400 ALREADY_MONITORING %s", SID_TAG);
            } else sprintf(response, "ERR 400 INVALID_PORT %s", SID_TAG);
        }
        else if (strcmp(buffer, "MONITOR STOP") == 0) {
            if (monitor_active) {
                monitor_active = 0;
                pthread_join(monitor_tid, NULL); 
                monitor_running = 0;
                sprintf(response, "OK MONITOR_STOPPED %s", SID_TAG);
            } else sprintf(response, "ERR 400 NOT_MONITORING %s", SID_TAG);
        }
        else if (strcmp(buffer, "QUIT") == 0) {
            if (monitor_running) {
                monitor_active = 0;
                pthread_join(monitor_tid, NULL);
                monitor_running = 0;
            }
            sprintf(response, "OK BYE %s", SID_TAG);
            send(sock, response, strlen(response), 0);
            printf("Controller disconnected gracefully.\n");
            log_event("Client disconnected via QUIT command.");
            break; 
        }
        else {
            sprintf(response, "ERR 404 UNKNOWN_COMMAND %s", SID_TAG);
        }

        send(sock, response, strlen(response), 0);
    }

    if (monitor_running) {
        monitor_active = 0;
        pthread_join(monitor_tid, NULL);
    }

    if (bytes_read == 0) {
        printf("Controller disconnected gracefully.\n");
        log_event("Client disconnected unexpectedly.");
    }
    else if (bytes_read < 0) perror("recv failed");

    close(sock);
    return NULL;
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) exit(EXIT_FAILURE);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) exit(EXIT_FAILURE);
    if (listen(server_fd, 5) < 0) exit(EXIT_FAILURE);

    printf("Agent started. Listening on port %d...\n", PORT);
    log_event("Agent started listening.");

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) continue;
        int *new_sock = malloc(sizeof(int));
        *new_sock = new_socket;
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) < 0) {
            free(new_sock);
            close(new_socket);
            continue;
        }
        pthread_detach(thread_id);
    }
    close(server_fd);
    return 0;
}
