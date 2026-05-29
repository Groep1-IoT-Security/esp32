import paho.mqtt.client as mqtt
import psycopg2

conn = psycopg2.connect(
    dbname="mqtt_data",
    user="postgres",
    password="DeHaagseHogeschool2026CSTIOT",
    host="db",  # Gebruik hier de servicenaam 'db'
    port="5432"
)

cursor = conn.cursor()

def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker")
    client.subscribe("#")  # alle topics

def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode()

    print(f"[MQTT] {topic}: {payload}")

    cursor.execute(
        "INSERT INTO sensor_data (topic, payload) VALUES (%s, %s)",
        (topic, payload)
    )
    conn.commit()

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect("mqtt", 1883, 60)
client.loop_forever()