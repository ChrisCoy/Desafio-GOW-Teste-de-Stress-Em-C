#define HTTPSERVER_IMPL
#include "httpserver.h"

void handle_request(struct http_request_s *request)
{
  // if (request_target_is(request, "/programadores"))
  // {
  //   handle_programadores_request(request);
  //   return;
  // }
  // if (request_target_is(request, "/contagem-programadores"))
  // {
  //   handle_count_request(request);
  //   return;
  // }

  struct http_response_s *response = http_response_init();
  char *body = "404 Not Found";
  http_response_header(response, "Content-Type", "text/plain; charset=UTF-8");
  http_response_body(response, body, strlen(body));
  // http_response_status(response, 404);
  http_respond(request, response);
}

int main()
{
  // printf("Starting server on port %d...\n", PORT);
  struct http_server_s *server = http_server_init(PORT, handle_request);
  http_server_listen(server);
}
