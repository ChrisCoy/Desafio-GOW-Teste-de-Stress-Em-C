#!/bin/bash
docker compose down
docker compose up --build -d
sleep 2
k6 run k6.js
docker compose down