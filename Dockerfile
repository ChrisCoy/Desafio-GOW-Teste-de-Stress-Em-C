# Dockerfile
FROM debian:bullseye-slim

RUN apt-get update && apt-get install -y \
    gcc \
    libpq-dev \
    libhiredis-dev \
    uuid-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY src/ .

RUN gcc api.c json.c -o server -I/usr/include/postgresql -lpq -lm -lhiredis -luuid

CMD ["./server"]
