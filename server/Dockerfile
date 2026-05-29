FROM python:3.9-slim

WORKDIR /app

# Installeer dependencies (inclusief psycopg2 voor Postgres)
RUN apt-get update && apt-get install -y libpq-dev gcc && \
    pip install flask flask-cors requests paho-mqtt psycopg2-binary

COPY . .

# We draaien de server op poort 5001 (zoals in je code staat)
EXPOSE 5001