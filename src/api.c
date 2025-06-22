#define HTTPSERVER_IMPL
#include "httpserver.h"
#include "json.h"
#include <string.h>
#include <libpq-fe.h>
#include <hiredis/hiredis.h>
#include <uuid/uuid.h>

// #define PG_HOST "db"
#define PG_HOST "host.docker.internal" // Use this for local development with Docker
// #define REDIS_HOST "redis"
#define REDIS_HOST  "host.docker.internal" // Use this for local development with Docker

#define PORT 8080

struct programador
{
  char nome[101];
  char apelido[33];
  char nascimento[11];
  char *stack;
};

PGconn *db_conn = NULL;
void connect_to_database()
{
  while (db_conn == NULL)
  {
    db_conn = PQconnectdb("host=" PG_HOST " port=5432 dbname=challenge user=postgres password=postgres");
    if (PQstatus(db_conn) != CONNECTION_OK)
    {
      fprintf(stderr, "Connection to database failed: %s", PQerrorMessage(db_conn));
      fflush(stderr);
      db_conn = NULL;
      sleep(2);
    }
  }

  printf("Connected to database successfully.\n");
  fflush(stdout);
}

redisContext *redisCtx = NULL;
void connect_to_redis()
{
  while (redisCtx == NULL)
  {
    redisCtx = redisConnect(REDIS_HOST, 6379);
    if (redisCtx == NULL || redisCtx->err)
    {
      if (redisCtx)
      {
        fprintf(stderr, "Connection to Redis failed: %s\n", redisCtx->errstr);
        fflush(stderr);
        redisFree(redisCtx);
      }
      redisCtx = NULL;
      sleep(2);
    }
  }

  printf("Connected to Redis successfully.\n");
  fflush(stdout);

  if (getenv("INSTANCE_ID") != NULL && strcmp(getenv("INSTANCE_ID"), "first") == 0)
  {
    printf("Instance is first, populating Redis with apelidos from database.\n");
    fflush(stdout);

    redisReply *reply = redisCommand(redisCtx, "DEL apelidos");
    if (reply == NULL)
    {
      fprintf(stderr, "Failed to clear apelidos in Redis: %s\n", redisCtx->errstr);
      fflush(stderr);
      redisFree(redisCtx);
      redisCtx = NULL;
      return;
    }
    freeReplyObject(reply);

    PGresult *res = PQexec(db_conn, "SELECT apelido FROM programadores");
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
      fprintf(stderr, "Failed to fetch programadores from database: %s\n", PQerrorMessage(db_conn));
      fflush(stderr);
      PQclear(res);
      redisFree(redisCtx);
      redisCtx = NULL;
      return;
    }

    printf("Fetched %d programadores apelidos from database.\n", PQntuples(res));
    fflush(stdout);

    for (int i = 0; i < PQntuples(res); i++)
    {
      const char *apelido = PQgetvalue(res, i, 0);
      redisReply *replyApelido = redisCommand(redisCtx, "SADD apelidos %s", apelido);
      if (replyApelido == NULL)
      {
        fprintf(stderr, "Failed to add apelido to Redis: %s\n", redisCtx->errstr);
        fflush(stderr);

        redisFree(redisCtx);
        redisCtx = NULL;
        PQclear(res);

        exit(EXIT_FAILURE);
        return;
      }
      freeReplyObject(replyApelido);
    }

    PQclear(res);
  }
}

int request_target_is(struct http_request_s *request, char const *target)
{
  http_string_t url = http_request_target(request);
  unsigned long len = strlen(target);
  return len == url.len && memcmp(url.buf, target, url.len) == 0;
}

int request_method_is(struct http_request_s *request, char const *method)
{
  http_string_t method_str = http_request_method(request);
  unsigned long len = strlen(method);
  return len == method_str.len && memcmp(method_str.buf, method, method_str.len) == 0;
}

void free_programador(struct programador *p)
{
  if (!p)
    return;
  if (p->stack)
    free(p->stack);
  free(p);
}
struct programador *extract_programador_from_json(json_value *value)
{
  if (value->type != json_object)
  {
    return NULL;
  }

  struct programador *prog = malloc(sizeof(struct programador));
  if (!prog)
  {
    free_programador(prog);
    return NULL;
  }

  for (unsigned int i = 0; i < value->u.object.length; ++i)
  {
    json_object_entry entry = value->u.object.values[i];

    if (strcmp(entry.name, "nome") == 0)
    {
      if (entry.value->type != json_string || entry.value->u.string.length == 0 || entry.value->u.string.length > 100)
      {
        free_programador(prog);
        return NULL;
      }

      strncpy(prog->nome, entry.value->u.string.ptr, sizeof(prog->nome) - 1);
      prog->nome[sizeof(prog->nome) - 1] = '\0';
      continue;
    }

    if (strcmp(entry.name, "apelido") == 0)
    {
      if (entry.value->type != json_string || entry.value->u.string.length == 0 || entry.value->u.string.length > 32)
      {
        free_programador(prog);
        return NULL;
      }

      strncpy(prog->apelido, entry.value->u.string.ptr, sizeof(prog->apelido) - 1);
      prog->apelido[sizeof(prog->apelido) - 1] = '\0';
      continue;
    }

    if (strcmp(entry.name, "nascimento") == 0)
    {
      if (entry.value->type != json_string || entry.value->u.string.length == 0 || entry.value->u.string.length > 10)
      {
        free_programador(prog);
        return NULL;
      }

      int year, month, day;
      if (sscanf(entry.value->u.string.ptr, "%d-%d-%d", &year, &month, &day) != 3 ||
          year < 1900 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31)
      {
        free_programador(prog);
        return NULL;
      }

      snprintf(prog->nascimento, sizeof(prog->nascimento), "%04d-%02d-%02d", year, month, day);
      prog->nascimento[sizeof(prog->nascimento) - 1] = '\0';
      continue;
    }

    if (strcmp(entry.name, "stack") == 0)
    {
      if (entry.value->type != json_array)
      {
        continue;
      }

      if (entry.value->u.array.length == 0)
      {
        prog->stack = strdup("{}");
        continue;
      }

      prog->stack = malloc((sizeof(char *) * 33 * entry.value->u.array.length) + 2);
      if (!prog->stack)
      {
        free_programador(prog);
        return NULL;
      }

      prog->stack[0] = '{';
      prog->stack[1] = '\0';

      for (unsigned int j = 0; j < entry.value->u.array.length; ++j)
      {
        json_value *stack_item = entry.value->u.array.values[j];
        if (stack_item->type != json_string || stack_item->u.string.length == 0 || stack_item->u.string.length > 32)
        {
          free_programador(prog);
          return NULL;
        }

        strcat(prog->stack, "\"");
        strcat(prog->stack, stack_item->u.string.ptr);
        strcat(prog->stack, "\"");

        if (j < entry.value->u.array.length - 1)
        {
          strcat(prog->stack, ",");
        }
      }

      strcat(prog->stack, "}");

      continue;
    }
  }

  return prog;
}

void handle_programadores_request(struct http_request_s *request)
{
  struct http_response_s *response = http_response_init();
  http_response_header(response, "Content-Type", "text/plain; charset=UTF-8");

  if (!request_method_is(request, "POST"))
  {
    http_response_status(response, 405);
    http_response_body(response, "Method Not Allowed", strlen("Method Not Allowed"));
    http_respond(request, response);
    return;
  }

  http_string_t body = http_request_body(request);
  if (body.len == 0)
  {
    http_response_status(response, 400);
    http_response_body(response, "Bad Request", strlen("Bad Request"));
    http_respond(request, response);
    return;
  }

  json_value *json = json_parse(body.buf, body.len);
  if (json == NULL)
  {
    http_response_status(response, 400);
    http_response_body(response, "JSON malformed", strlen("JSON malformed"));
    http_respond(request, response);
    return;
  }

  struct programador *prog = extract_programador_from_json(json);
  if (prog == NULL)
  {
    http_response_status(response, 400);
    http_response_body(response, "Invalid programador data", strlen("Invalid programador data"));
    http_respond(request, response);
    return;
  }
  json_value_free(json);

  redisReply *containsApelido = redisCommand(redisCtx, "SISMEMBER apelidos %s", prog->apelido);
  if (containsApelido->type == REDIS_REPLY_INTEGER && containsApelido->integer == 1)
  {
    freeReplyObject(containsApelido);
    free_programador(prog);

    http_response_status(response, 422);
    http_response_body(response, "Unprocessable Entity", strlen("Unprocessable Entity"));
    http_respond(request, response);
    return;
  }

  redisReply *reply = redisCommand(redisCtx, "SADD apelidos %s", prog->apelido);
  if (reply == NULL)
  {
    fprintf(stderr, "Erro ao inserir apelido no Redis: %s\n", redisCtx->errstr);
    fflush(stderr);
    free_programador(prog);

    http_response_status(response, 500);
    http_response_body(response, "Internal Server Error", strlen("Internal Server Error"));
    http_respond(request, response);
    return;
  }
  freeReplyObject(reply);

  uuid_t uuid;
  char uuid_str[37];

  uuid_generate(uuid);
  uuid_unparse(uuid, uuid_str);

  const char *paramValues[5] = {
      uuid_str,
      prog->nome,
      prog->apelido,
      prog->nascimento,
      prog->stack,
  };

  PGresult *res = PQexecParams(
      db_conn,
      "INSERT INTO programadores (id, nome, apelido, nascimento, stack) VALUES ($1, $2, $3, $4, $5::text[]);",
      5,
      NULL,
      paramValues,
      NULL,
      NULL,
      0);

  if (PQresultStatus(res) != PGRES_COMMAND_OK)
  {
    fprintf(stderr, "Erro na inserção: %s", PQerrorMessage(db_conn));
    fflush(stderr);
    PQclear(res);
    free_programador(prog);

    http_response_status(response, 500);
    http_response_body(response, "Internal Server Error", strlen("Internal Server Error"));
    http_respond(request, response);
    return;
  }

  printf("Programador %s %s inserido com sucesso.\n", prog->nome, prog->apelido);
  fflush(stdout);

  free_programador(prog);
  PQclear(res);

  http_response_status(response, 201);
  http_response_body(response, "ok", strlen("ok"));
  http_respond(request, response);
  return;
}

void handle_count_request(struct http_request_s *request)
{
  struct http_response_s *response = http_response_init();
  http_response_header(response, "Content-Type", "text/plain; charset=UTF-8");

  if (!request_method_is(request, "GET"))
  {
    http_response_status(response, 405);
    http_response_body(response, "Method Not Allowed", strlen("Method Not Allowed"));
    http_respond(request, response);
    return;
  }

  PGresult *res = PQexec(db_conn, "SELECT count(*) FROM programadores;");

  if (PQresultStatus(res) != PGRES_TUPLES_OK)
  {
    fprintf(stderr, "Erro na query: %s", (char *)PQerrorMessage(db_conn));
    fflush(stderr);
    PQclear(res);

    http_response_status(response, 500);
    http_response_body(response, "Internal Server Error", strlen("Internal Server Error"));
    http_respond(request, response);
    return;
  }

  char *count = (char *)PQgetvalue(res, 0, 0);
  http_response_status(response, 200);
  http_response_body(response, count, strlen(count));
  http_respond(request, response);
  PQclear(res);
}

void handle_request(struct http_request_s *request)
{
  if (request_target_is(request, "/programadores"))
  {
    handle_programadores_request(request);
    return;
  }
  if (request_target_is(request, "/contagem-programadores"))
  {
    handle_count_request(request);
    return;
  }

  struct http_response_s *response = http_response_init();
  char *body = "404 Not Found";
  http_response_header(response, "Content-Type", "text/plain; charset=UTF-8");
  http_response_body(response, body, strlen(body));
  http_response_status(response, 404);
  http_respond(request, response);
}

int main()
{
  connect_to_database();
  connect_to_redis();

  printf("Starting server on port %d...\n", PORT);
  fflush(stdout);
  struct http_server_s *server = http_server_init(PORT, handle_request);
  http_server_listen(server);
  PQfinish(db_conn);
  redisFree(redisCtx);
}
