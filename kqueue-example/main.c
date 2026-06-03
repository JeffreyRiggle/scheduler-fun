#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>

const char *get_response = 
  "HTTP/1.1 200 OK\r\n"
  "Content-Type: text/html; charset=utf-8\r\n"
  "Content-Length: 191\r\n"
  "Connection: close\r\n"
  "\r\n"
  "<html><body><form action=\"file\" method=\"post\"><label for=\"fscript\">Create a script</label><input type=\"text\" name=\"fscript\" required /><input type=\"submit\" value=\"send\"/></form></body></html>";

const char *post_response = 
  "HTTP/1.1 200 OK\r\n"
  "Content-Type: text/html; charset=utf-8\r\n"
  "Content-Length: %d\r\n"
  "Connection: keep-alive\r\n"
  "Keep-Alive: timeout=5\r\n"
  "\r\n"
  "<html><script>%s</script></html>";

typedef struct {
  char* method;
  char* path;
} http_header_details_t;

typedef struct {
  int len;
  char* body;
} tcp_buf_t;

typedef struct {
  char* data;
  int len;
} script_data_t;


http_header_details_t get_header_details(const tcp_buf_t *buf) {
  int iter = 0;
  int method_offset = 0;
  int path_offset = 0;
  http_header_details_t details;

  while (iter < buf->len) {
    if (buf->body[iter] == ' ') {
      if (method_offset == 0) {
        method_offset = iter;
        char* method = malloc(method_offset + 1);
        memcpy(method, buf->body, method_offset);
        method[method_offset + 1] = '\0';
        details.method = method;
      }
      else if (path_offset == 0) {
        path_offset = iter;
        int path_len = path_offset - (method_offset + 1);
        char* path = malloc(path_len);
        memcpy(path, buf->body + method_offset + 1, path_len);
        details.path = path;
      } else {
        break;
      }
    }

    iter++;
  }

  return details;
}

size_t url_decode(char *str) {
    char *p = str;
    char *q = str;
    while (*q) {
        if (*q == '%') {
            if (q[1] && q[2] && isxdigit(q[1]) && isxdigit(q[2])) {
                char hex[3] = {q[1], q[2], '\0'};
                *p = (char)strtol(hex, NULL, 16);
                q += 2;
            }
        } else if (*q == '+') {
            *p = ' ';
        } else {
            *p = *q;
        }
        p++;
        q++;
    }
    *p = '\0';

    return (size_t)(p - str);
}

script_data_t extract_script(const tcp_buf_t *buf) {
  script_data_t data;
  int iter = 0;
  int script_size = 0;
  char* script = 0;

  while (iter < buf->len) {
    if (buf->body[iter] == '\n') {
      int base_offset = iter + 1;
      if (strncmp(buf->body + base_offset, "fscript", 7) == 0) {
        script_size = -7;
        int innerIter = base_offset;
        while (innerIter < buf->len) {
          if (buf->body[innerIter] == '\n' || buf->body[innerIter] == '\0') {
            break;
          }
          script_size++;
          innerIter++;
        }

        script = malloc(script_size + 1);
        memcpy(script, buf->body + base_offset + 8, script_size);
        script[script_size + 1] = '\0';
        break;
      }
    }
    iter++;
  }

  if (script == 0) {
    // Bad request
    printf("No script found\n");
    return data;
  }

  data.data = script;
  data.len = script_size;

  data.len = url_decode(data.data);
  return data;
}

char* generate_post_response(script_data_t script) {
  int len = snprintf(NULL, 0, (char *)post_response, script.len + 30, script.data);
  char* response = malloc(len + 1);
  snprintf(response, len + 1, (char *)post_response, script.len + 30, script.data);

  return response;
}

void handle_post_file_request(int client_fd, const tcp_buf_t *buf) {
  script_data_t script = extract_script(buf);
  char* res = generate_post_response(script);
  write(client_fd, res, strlen(res));
}

bool is_home_page_request(http_header_details_t details) {
  return strcmp(details.method, "GET") == 0 && strcmp(details.path, "/") == 0;
}

bool is_post_file_request(http_header_details_t details) {
  return strcmp(details.method, "POST") == 0 && strcmp(details.path, "/file") == 0;
}

void handle_home_request(int client) {
  write(client, get_response, strlen(get_response));
}

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
      return flags;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK); 
}

void process_event(struct kevent evt, int server_fd, struct kevent* change_event, int kqueue_fd) {
  int target_fd = evt.ident;

  if (evt.flags & EV_EOF) {
    printf("Client has disconnected closing fd %d\n", target_fd);
    close(target_fd);
    return; 
  }

  if (target_fd == server_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

    if (client_fd == -1) {
      fprintf(stderr, "Failed to accept connection!\n");
      return; 
    }

    int nonblocking_res = set_nonblocking(client_fd);
    if (nonblocking_res == -1) {
      fprintf(stderr, "Failed to set client connection to nonblocking terminiating connection\n");
      close(client_fd);
      return;
    }

    EV_SET(change_event, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
    if (kevent(kqueue_fd, change_event, 1, NULL, 0, NULL) == -1) {
      fprintf(stderr, "Failed to register client to a new socket\n");
      close(client_fd);
    }
    return;
  }

  if (evt.filter == EVFILT_READ) {
    char* buffer = malloc(1024);
    ssize_t bytes_read = read(target_fd, buffer, 1023);
    if (bytes_read > 0) {
      tcp_buf_t tcp_buffer;
      tcp_buffer.body = buffer;
      tcp_buffer.len = 1023;

      http_header_details_t header_details = get_header_details(&tcp_buffer);
      if (is_home_page_request(header_details)) {
        handle_home_request(target_fd);
      } else if (is_post_file_request(header_details)) {
        handle_post_file_request(target_fd, &tcp_buffer);
      } else {
        // Unhandled request
        return;
      }

      return;
    }

    if (bytes_read == 0) {
      printf("Unable to read data killing connection or something\n");
      close(target_fd);
      return;
    }
    printf("Failed to read data\n");
  }
}

int connect_server() {
  struct sockaddr_in server_addr;
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    fprintf(stderr, "Failed to socket for TCP server\n");
    return -1;
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(3000);

  int bind_res = bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (bind_res < 0) {
    fprintf(stderr, "Failed to bind server to socket %d. Closing server socket!\n", bind_res);
    close(server_fd);
    return -1;
  }

  int listen_res = listen(server_fd, SOMAXCONN);
  if (listen_res < 0) {
    fprintf(stderr, "Failed to listen on TCP server %d. Closing server socket!\n", listen_res);
    close(server_fd);
    return -1;
  }

  int nonblocking_res = set_nonblocking(server_fd);
  if (nonblocking_res == -1) {
    fprintf(stderr, "Failed to set to nonblocking mode. Closing server socket!\n");
    return -1;
  }

  return server_fd;
}

int main() {
  int kqueue_fd;
  struct kevent change_event, event_list[32];

  int server_fd = connect_server();
  printf("Server is listenting on port 3000...\n");

  kqueue_fd = kqueue();
  if (kqueue_fd == -1) {
    fprintf(stderr, "failed to create queue. Closing server socket!\n");
    close(server_fd);
    return -1;
  }

  EV_SET(&change_event, server_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
  if (kevent(kqueue_fd, &change_event, 1, NULL, 0, NULL) == -1) {
    fprintf(stderr, "Failed to register socket closing server socket!\n");
    close(server_fd);
    return -1;
  }

  while (1) {
    int event_count = kevent(kqueue_fd, NULL, 0, event_list, 32, NULL);
    if (event_count == -1) {
      fprintf(stderr, "Failed to wait on kqueue\n");
      break;
    }

    for (int i = 0; i < event_count; i++) {
      struct kevent target_event = event_list[i];
      process_event(target_event, server_fd, &change_event, kqueue_fd);
    }
  }

  close(server_fd);
  close(kqueue_fd);
  return 0;
}
