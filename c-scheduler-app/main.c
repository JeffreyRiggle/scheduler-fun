#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <uv.h>
#include <sys/time.h>
#include <fcntl.h>

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
} HttpHeaderDetails;

void on_write_end(uv_write_t *req, int status) {
  if (status < 0) {
    fprintf(stderr, "Failed to write %s\n", uv_strerror(status));
  }

  free(req);
}

HttpHeaderDetails get_header_details(const uv_buf_t *buf) {
  int iter = 0;
  int method_offset = 0;
  int path_offset = 0;
  HttpHeaderDetails details;

  while (iter < buf->len) {
    if (buf->base[iter] == ' ') {
      if (method_offset == 0) {
        method_offset = iter;
        char* method = malloc(method_offset + 1);
        memcpy(method, buf->base, method_offset);
        method[method_offset + 1] = '\0';
        details.method = method;
      }
      else if (path_offset == 0) {
        path_offset = iter;
        int path_len = path_offset - (method_offset + 1);
        char* path = malloc(path_len);
        memcpy(path, buf->base + method_offset + 1, path_len);
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

typedef struct {
  char* data;
  int len;
} ScriptData;

ScriptData extract_script(const uv_buf_t *buf) {
  ScriptData data;
  int iter = 0;
  int script_size = 0;
  char* script = 0;

  while (iter < buf->len) {
    if (buf->base[iter] == '\n') {
      int base_offset = iter + 1;
      if (strncmp(buf->base + base_offset, "fscript", 7) == 0) {
        script_size = -7;
        int innerIter = base_offset;
        while (innerIter < buf->len) {
          if (buf->base[innerIter] == '\n' || buf->base[innerIter] == '\0') {
            break;
          }
          script_size++;
          innerIter++;
        }

        script = malloc(script_size + 1);
        memcpy(script, buf->base + base_offset + 8, script_size);
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

char* generate_post_response(ScriptData script) {
  int len = snprintf(NULL, 0, (char *)post_response, script.len + 30, script.data);
  char* response = malloc(len + 1);
  snprintf(response, len + 1, (char *)post_response, script.len + 30, script.data);

  return response;
}

bool is_home_page_request(HttpHeaderDetails details) {
  return strcmp(details.method, "GET") == 0 && strcmp(details.path, "/") == 0;
}

bool is_post_file_request(HttpHeaderDetails details) {
  return strcmp(details.method, "POST") == 0 && strcmp(details.path, "/file") == 0;
}

void handle_home_request(uv_stream_t *client) {
  uv_write_t *write_req = (uv_write_t *)malloc(sizeof(uv_write_t));
  uv_buf_t response_buf = uv_buf_init((char *)get_response, strlen(get_response));

  uv_write(write_req, client, &response_buf, 1, on_write_end);
}

void on_file_write(uv_fs_t* req) {
  if (req-> result < 0) {
    fprintf(stderr, "Failed to write to file: %s\n", uv_strerror(req->result));
    return;
  }

  uv_fs_t close_req;
  uv_fs_close(uv_default_loop(), &close_req, (uv_file)(intptr_t)req->data, NULL);
}

void on_file_open(uv_fs_t* req) {
  if (req->result < 0) {
    fprintf(stderr, "Failed to open file: %s\n", uv_strerror(req->result));
    return;
  }

  uv_file fd = req->result;
  uv_fs_req_cleanup(req);

  ScriptData* data = req->data;
  printf("Getting ready to write file of size %d, with content %s\n", data->len, data->data);
  uv_buf_t buffers[2];
  buffers[0] = uv_buf_init(data->data, data->len);
  buffers[1] = uv_buf_init("\n", 1);
  uv_fs_t* write_req = malloc(sizeof(uv_fs_t));
  write_req->data = (void*)(intptr_t)fd;

  uv_fs_write(uv_default_loop(), write_req, fd, buffers, 1, -1, on_file_write);
}

long long current_time_ms() {
  struct timeval tv;
  gettimeofday(&tv, NULL);

  return ((long long)tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

void handle_post_file_request(uv_stream_t *client, const uv_buf_t *buf) {
  ScriptData script = extract_script(buf);
  char* res = generate_post_response(script);
  uv_write_t *write_req = (uv_write_t *)malloc(sizeof(uv_write_t));
  uv_buf_t response_buf = uv_buf_init((char *)res, strlen(res));

  uv_write(write_req, client, &response_buf, 1, on_write_end);

  uv_fs_t* open_req = malloc(sizeof(uv_fs_t));
  open_req->data = &script;

  char file_name[256];
  snprintf(file_name, sizeof(file_name), "./submission-%lld.js", current_time_ms());
  uv_fs_open(uv_default_loop(), open_req, file_name, O_CREAT | O_WRONLY, 0666, on_file_open);
}

void on_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
  if (nread > 0) {
    printf("Incoming request:\n%.*s\n", (int)nread, buf->base);

    HttpHeaderDetails details = get_header_details(buf);
    if (is_home_page_request(details)) {
      handle_home_request(client);
    } else if (is_post_file_request(details)) {
      handle_post_file_request(client, buf);
    }
    else {
      fprintf(stderr, "Invalid method %s or path %s\n", details.method, details.path);
      free(buf->base);
      return;
    }
  } else if (nread < 0) {
    if (nread != UV_EOF) {
      fprintf(stderr, "Failed to read: %s\n", uv_strerror(nread));
      uv_close((uv_handle_t *)client, (uv_close_cb)free);
    }
  }

  if (buf->base) {
    free(buf->base);
  }
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  buf->base = (char *)malloc(suggested_size);
  buf->len = suggested_size;
}

void on_new_connection(uv_stream_t *server, int status) {
  if (status < 0) {
    fprintf(stderr, "Connection error: %s\n", uv_strerror(status));
    return;
  }

  uv_tcp_t *client = (uv_tcp_t *)malloc(sizeof(uv_tcp_t));
  uv_tcp_init(server->loop, client);

  if (uv_accept(server, (uv_stream_t*)client) == 0) {
    uv_read_start((uv_stream_t *)client, alloc_buffer, on_read);
  } else {
    uv_close((uv_handle_t *)client, (uv_close_cb)free);
  }
}

int main() {
    uv_loop_t *loop = uv_default_loop();

    uv_tcp_t server;
    uv_tcp_init(loop, &server);
    struct sockaddr_in addr;

    uv_ip4_addr("0.0.0.0", 3000, &addr);
    uv_tcp_bind(&server, (const struct sockaddr *)&addr, 0);
    int r = uv_listen((uv_stream_t *)&server, 128, on_new_connection);
    if (r) {
      fprintf(stderr, "Error while listening: %s\n", uv_strerror(r));
    }

    printf("Started listening...\n");

    return uv_run(loop, UV_RUN_DEFAULT);
}
