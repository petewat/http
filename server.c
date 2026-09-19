#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct sockaddr_in addr;
int addrlen = sizeof(addr);

int server_fd;

const int buffer_size = 5000;
char buffer[buffer_size];
char body_buffer[buffer_size];
char method[16];
char path[256];
char protocol[16];

char file_buffer[500000];
char header[256];

void error404(int socket) {
  char *not_found = "HTTP/1.1 404 Not Found\r\nConnection: "
                    "close\r\nContent-Length: 0\r\n\r\n";
  write(socket, not_found, strlen(not_found));
  close(socket);
  exit(1);
}

void error405(int socket) {
  char *not_allowed = "HTTP/1.1 405 Method Not Allowed\r\nConnection: "
                      "close\r\nContent-Length: 0\r\n\r\n";
  write(socket, not_allowed, strlen(not_allowed));
  close(socket);
  exit(1);
}

char *get_type(char *file_path) {
  if (strstr(file_path, ".html"))
    return "text/html";
  if (strstr(file_path, ".jpg"))
    return "image/jpeg";
  if (strstr(file_path, ".csv"))
    return "text/csv";
  return "application/octet-stream";
}

void init() {
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(8888);
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("Could not create a socket\n");
    exit(1);
  }
  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Binding failed");
    exit(1);
  }
  // printf("init succesful\n");
}

void get(char *path, int client_socket) {
  char file_to_open[512];
  char *content_type;
  char *pref = "output";
  if (!strcmp(path, "/"))
    snprintf(file_to_open, sizeof(file_to_open), "output/index.html");
  else
    snprintf(file_to_open, sizeof(file_to_open), "output%s", path);
  if (strstr(file_to_open, "..") != NULL) {
    // printf("invalid path\n");
    close(client_socket);
    return;
  }
  // printf("%s\n",file_to_open);

  content_type = get_type(file_to_open);

  FILE *data = fopen(file_to_open, "rb");
  if (data == NULL)
    error404(client_socket);
  size_t file_size = fread(file_buffer, 1, sizeof(file_buffer), data);

  snprintf(header, sizeof(header),
           "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: "
           "%s\r\nContent-Length: %zu\r\n\r\n",
           content_type, file_size);
  write(client_socket, header, strlen(header));
  write(client_socket, file_buffer, file_size);
  fclose(data);
}

void post(char *body, int client_socket) {
  body = strstr(body, "=");
  if (body != NULL) {
    body++;
    // printf("%s\n",body);
  }
  char *go_back =
      "HTTP/1.1 303 See Other\r\nLocation:/\r\nContent-Length: 0\r\n\r\n";
  write(client_socket, go_back, strlen(go_back));
}

int read_header(int client_socket) {
  int total_read = 0;
  char *header_end;
  while (total_read < buffer_size - 1) {
    int bytes_read =
        read(client_socket, buffer + total_read, buffer_size - 1 - total_read);
    if (bytes_read < 0) {
      perror("Error reading from socket");
      break;
    }
    if (bytes_read == 0)
      break;
    total_read += bytes_read;
    buffer[total_read] = '\0';
    header_end = strstr(buffer, "\r\n\r\n");
    if (header_end != NULL)
      break;
  }
  if (header_end == NULL) {
    error404(client_socket);
  }
  return total_read;
}

void read_body(int client_socket, char *header_end, int total_read) {
  // reading the request body
  int content_length = 0;
  char *content_length_ptr = strstr(buffer, "Content-Length:");
  if (content_length_ptr != NULL)
    sscanf(content_length_ptr, "Content-Length: %d", &content_length);
  if (content_length >= buffer_size) {
    perror("Body buffer overload\n");
    close(client_socket);
    return;
  }
  if (content_length > 0) {
    char *body = header_end + 4;
    int already_read = (buffer + total_read) - body;
    if (already_read > 0)
      memcpy(body_buffer, body, already_read);
    while (already_read < content_length && already_read < buffer_size - 1) {
      int bytes_read = read(client_socket, body_buffer + already_read,
                            content_length - already_read);
      if (bytes_read <= 0)
        break;
      already_read += bytes_read;
    }
    body_buffer[already_read] = '\0';
  }
}

void handle_client(int client_socket) {
  // reading the request header
  int total_read = read_header(client_socket);
  char *header_end = strstr(buffer, "\r\n\r\n");

  sscanf(buffer, "%s %s %s", method, path, protocol);
  // printf("Method: %15s\n",method);
  // printf("Path: %255s\n",path);
  // printf("Protocol: %15s\n",protocol);

  if (!strcmp(method, "GET"))
    get(path, client_socket);
  else if (!strcmp(method, "POST")) {
    read_body(client_socket, header_end, total_read);
    post(body_buffer, client_socket);
  } else
    error405(client_socket);

  // printf("Connection closed\n");
}

void run_forever() {
  while (1) {
    int client_socket =
        accept(server_fd, (struct sockaddr *)&addr, (socklen_t *)&addrlen);
    if (client_socket < 0) {
      // printf("Unsuccessful connection\n");
      continue;
    }
    // printf("Accepted connection\n");
    pid_t pid = fork();
    if (pid < 0) {
      perror("fork unsuccessful\n");
      close(client_socket);
      continue;
    }

    if (pid == 0) {
      // child;
      // printf("child process\n");
      close(server_fd);
      // printf("HERE\n");
      handle_client(client_socket);
      close(client_socket);
      exit(0);
    } else {
      // printf("parent process\n");
      close(client_socket);
    }
  }
}
int main() {
  init();
  listen(server_fd, SOMAXCONN);
  if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
    perror("signal");
    exit(1);
  }
  run_forever();
}

/*
tested on:
wrk -t2 -c1000 -d10s http://localhost:8888/
*/