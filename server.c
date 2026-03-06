#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "db_utils.h"

#define DEFAULT_PORT 8080
#define BUFFER_SIZE (1024 * 1024 * 5) // 5MB buffer for large Base64 images
#define MAX_PATH 1024

const char *HTTP_200 = "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nConnection: close\r\n\r\n";
const char *HTTP_404 = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n404 Not Found";
const char *HTTP_400 = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n400 Bad Request";
const char *HTTP_500 = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n500 Server Error";

const char* get_content_type(const char* path) {
    if (strstr(path, ".html")) return "text/html; charset=utf-8";
    if (strstr(path, ".css")) return "text/css; charset=utf-8";
    if (strstr(path, ".js")) return "application/javascript; charset=utf-8";
    if (strstr(path, ".json")) return "application/json; charset=utf-8";
    return "text/plain; charset=utf-8";
}

// Convert a hex digit to integer
int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// Decode JSON \uXXXX to UTF-8
int decode_unicode_escape(const char **in, char **out) {
    // Read 4 hex digits
    int code_point = 0;
    for (int i = 0; i < 4; i++) {
        if (!(*in)[i]) return 0; // Unexpected end
        code_point = (code_point << 4) | hex_to_int((*in)[i]);
    }
    *in += 4;

    // Convert code_point to UTF-8 bytes
    if (code_point <= 0x7F) {
        *(*out)++ = (char)code_point;
    } else if (code_point <= 0x7FF) {
        *(*out)++ = (char)(0xC0 | (code_point >> 6));
        *(*out)++ = (char)(0x80 | (code_point & 0x3F));
    } else {
        *(*out)++ = (char)(0xE0 | (code_point >> 12));
        *(*out)++ = (char)(0x80 | ((code_point >> 6) & 0x3F));
        *(*out)++ = (char)(0x80 | (code_point & 0x3F));
    }
    return 1;
}

void get_json_string(const char* json, const char* key, char* out, size_t out_size) {
    out[0] = '\0';
    if (!json) return;
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\":\"", key);
    const char* start = strstr(json, search_key);
    
    if (start) {
        start += strlen(search_key);
        char* out_ptr = out;
        const char* in_ptr = start;
        size_t written = 0;
        
        while (*in_ptr && written < out_size - 1) {
            // Stop at unescaped quote
            if (*in_ptr == '"') {
                break;
            }
            
            if (*in_ptr == '\\') {
                in_ptr++; // skip backslash
                if (*in_ptr == '\0') break; // malformed
                
                if (*in_ptr == 'u') {
                    in_ptr++; // skip 'u'
                    if (!decode_unicode_escape(&in_ptr, &out_ptr)) {
                        break; // malformed unicode
                    }
                    written = out_ptr - out;
                    continue; // decode_unicode_escape already advances in_ptr
                } else if (*in_ptr == 'n') { *out_ptr++ = '\n'; }
                else if (*in_ptr == 'r') { *out_ptr++ = '\r'; }
                else if (*in_ptr == 't') { *out_ptr++ = '\t'; }
                else if (*in_ptr == '"') { *out_ptr++ = '"'; }
                else if (*in_ptr == '\\') { *out_ptr++ = '\\'; }
                else if (*in_ptr == '/') { *out_ptr++ = '/'; } // CRITICAL for Base64 (data:image\/jpeg...)
                else { 
                    *out_ptr++ = *in_ptr; // just copy unknown escapes
                }
                in_ptr++;
            } else {
                *out_ptr++ = *in_ptr++;
            }
            written = out_ptr - out;
        }
        *out_ptr = '\0';
    }
}

// Check if currently within Business Hours (Mon-Fri, 10:00 - 17:00, Turkey Time UTC+3)
int is_business_hours() {
    time_t rawtime;
    time(&rawtime);
    time_t turkey_time = rawtime + (3 * 3600);
    struct tm *info = gmtime(&turkey_time);
    if (info->tm_wday == 0 || info->tm_wday == 6) return 0; // Weekend
    if (info->tm_hour >= 10 && info->tm_hour < 17) return 1;
    return 0;
}

void send_response(int client_sock, const char* header_tpl, const char* content_type, const char* body) {
    char header[512];
    snprintf(header, sizeof(header), header_tpl, content_type);
    write(client_sock, header, strlen(header));
    if (body) {
        write(client_sock, body, strlen(body));
    }
}

void serve_static_file(int client_sock, const char* path) {
    char file_path[MAX_PATH];
    if (strcmp(path, "/") == 0) {
        snprintf(file_path, sizeof(file_path), "public/index.html");
    } else {
        snprintf(file_path, sizeof(file_path), "public%s", path);
    }
    
    // Simple directory traversal prevention
    if (strstr(file_path, "..")) {
        write(client_sock, HTTP_404, strlen(HTTP_404));
        return;
    }

    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        write(client_sock, HTTP_404, strlen(HTTP_404));
        return;
    }

    send_response(client_sock, HTTP_200, get_content_type(file_path), NULL);
    
    char file_buf[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(fd, file_buf, sizeof(file_buf))) > 0) {
        write(client_sock, file_buf, bytes_read);
    }
    close(fd);
}

void* handle_client(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);

    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        close(client_sock);
        return NULL;
    }
    
    ssize_t bytes_read = read(client_sock, buffer, BUFFER_SIZE - 1);
    if (bytes_read <= 0) {
        free(buffer);
        close(client_sock);
        return NULL;
    }
    buffer[bytes_read] = '\0';
    
    char method[16], path[MAX_PATH];
    if (sscanf(buffer, "%15s %1023s", method, path) != 2) {
        write(client_sock, HTTP_400, strlen(HTTP_400));
        free(buffer);
        close(client_sock);
        return NULL;
    }

    long content_length = 0;
    char* cl_ptr = strstr(buffer, "Content-Length:");
    if (!cl_ptr) cl_ptr = strstr(buffer, "content-length:");
    if (cl_ptr) {
        sscanf(cl_ptr + 15, " %ld", &content_length);
    }

    char* body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4;
        long headers_len = body - buffer;
        long currently_read_body = bytes_read - headers_len;
        
        while (currently_read_body < content_length && bytes_read < BUFFER_SIZE - 1) {
            ssize_t more = read(client_sock, buffer + bytes_read, BUFFER_SIZE - 1 - bytes_read);
            if (more <= 0) break;
            bytes_read += more;
            currently_read_body += more;
            buffer[bytes_read] = '\0';
        }
    }

    if (strcmp(method, "GET") == 0) {
        if (strncmp(path, "/api/poll?", 10) == 0) {
            char token[128] = {0};
            int after_id = 0;
            const char* t_start = strstr(path, "token=");
            if(t_start) sscanf(t_start, "token=%127[^&]", token);
            const char* a_start = strstr(path, "after=");
            if(a_start) sscanf(a_start, "after=%d", &after_id);
            
            // Return 401 if token is invalid (patient was kicked)
            if (get_user_id_by_token(token) == -1) {
                const char* r401 = "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"kicked\"}";
                write(client_sock, r401, strlen(r401));
            } else {
                char* j_res = db_get_messages(token, after_id);
                send_response(client_sock, HTTP_200, "application/json", j_res);
                free(j_res);
            }
        } else if (strncmp(path, "/api/admin/users", 16) == 0) {
             char* j_res = db_get_users();
             send_response(client_sock, HTTP_200, "application/json", j_res);
             free(j_res);
        } else if (strncmp(path, "/api/admin/poll?", 16) == 0) {
             int user_id = 0;
             int after_id = 0;
             const char* u_start = strstr(path, "user_id=");
             if(u_start) sscanf(u_start, "user_id=%d", &user_id);
             const char* a_start = strstr(path, "after=");
             if(a_start) sscanf(a_start, "after=%d", &after_id);
             
             char* j_res = db_get_messages_admin(user_id, after_id);
             send_response(client_sock, HTTP_200, "application/json", j_res);
             free(j_res);
        } else {
            serve_static_file(client_sock, path);
        }
    } else if (strcmp(method, "POST") == 0 && body) {
        if (strcmp(path, "/api/login") == 0) {
            char fname[128]="", sname[128]="", phone[128]="";
            get_json_string(body, "first_name", fname, sizeof(fname));
            get_json_string(body, "surname", sname, sizeof(sname));
            get_json_string(body, "phone", phone, sizeof(phone));
            
            char token[128];
            if (db_create_user(fname, sname, phone, token)) {
                char res[256];
                snprintf(res, sizeof(res), "{\"token\":\"%s\"}", token);
                send_response(client_sock, HTTP_200, "application/json", res);
            } else {
                write(client_sock, HTTP_500, strlen(HTTP_500));
            }
        } else if (strcmp(path, "/api/send") == 0) {
            char token[128]="";
            char *msg = malloc(BUFFER_SIZE);
            if (msg) {
                get_json_string(body, "token", token, sizeof(token));
                get_json_string(body, "msg", msg, BUFFER_SIZE);
                
                if (strlen(msg) > 0) {
                    if (is_business_hours()) {
                        db_save_message(token, 0, msg);
                        send_response(client_sock, HTTP_200, "application/json", "{\"status\":\"ok\"}");
                    } else {
                        // Send an error message back to the client if outside business hours
                        send_response(client_sock, HTTP_200, "application/json", 
                            "{\"error\":\"عذراً، العيادة مغلقة حالياً. أوقات الدوام من الإثنين للجمعة (10 صباحاً - 5 مساءً).\"}");
                    }
                } else {
                    send_response(client_sock, HTTP_200, "application/json", "{\"error\":\"Empty message\"}");
                }
                free(msg);
            }
        } else if (strcmp(path, "/api/admin/send") == 0) {
            char uid_str[32]="", pass[128]="";
            char *msg = malloc(BUFFER_SIZE);
            if (msg) {
                get_json_string(body, "password", pass, sizeof(pass));
                if (strcmp(pass, "Re.Re1020.") != 0) {
                    send_response(client_sock, HTTP_200, "application/json", "{\"error\":\"Unauthorized\"}");
                } else {
                    get_json_string(body, "user_id", uid_str, sizeof(uid_str));
                    get_json_string(body, "msg", msg, BUFFER_SIZE);
                    if (strlen(msg) > 0) {
                        db_save_message_admin(atoi(uid_str), msg);
                    }
                    send_response(client_sock, HTTP_200, "application/json", "{\"status\":\"ok\"}");
                }
                free(msg);
            }
        } else if (strcmp(path, "/api/logout") == 0) {
            char token[128]="";
            get_json_string(body, "token", token, sizeof(token));
            db_delete_user_by_token(token);
            send_response(client_sock, HTTP_200, "application/json", "{\"status\":\"ok\"}");
        } else if (strcmp(path, "/api/admin/kick") == 0) {
            char uid_str[32]="", pass[128]="";
            get_json_string(body, "password", pass, sizeof(pass));
            if (strcmp(pass, "Re.Re1020.") != 0) {
                send_response(client_sock, HTTP_200, "application/json", "{\"error\":\"Unauthorized\"}");
            } else {
                get_json_string(body, "user_id", uid_str, sizeof(uid_str));
                db_delete_user(atoi(uid_str));
                send_response(client_sock, HTTP_200, "application/json", "{\"status\":\"ok\"}");
            }
        } else {
            write(client_sock, HTTP_404, strlen(HTTP_404));
        }
    } else {
        write(client_sock, HTTP_400, strlen(HTTP_400));
    }

    free(buffer);
    close(client_sock);
    return NULL;
}

int main(int argc, char* argv[]) {
    // Determine port
    int port = DEFAULT_PORT;
    char *env_port = getenv("PORT");
    if (env_port) {
        port = atoi(env_port);
    }

    // Initialize Database
    db_init("randvu.db");
    
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    
    if (listen(server_sock, 10) < 0) {
        perror("Listen failed");
        exit(1);
    }
    
    printf("Randvu Server listening on port %d...\n", port);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_sock < 0) continue;
        
        int *new_sock = malloc(sizeof(int));
        *new_sock = client_sock;
        
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) != 0) {
            close(client_sock);
            free(new_sock);
        } else {
            pthread_detach(thread_id);
        }
    }
    
    db_close();
    return 0;
}
