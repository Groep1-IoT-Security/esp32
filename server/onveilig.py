import paho.mqtt.client as mqtt
import psycopg2

# ---------- PostgreSQL connectie ----------
conn = psycopg2.connect(
    dbname="mqtt_data",
    user="postgres",
    password="DeHaagseHogeschool2026CSTIOT",
    host="localhost",
    port="5432"
)
cursor = conn.cursor()

# ---------- MQTT callbacks ----------
def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker")
    client.subscribe("#")  # subscribe op alle topics

def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode()

    print(f"[MQTT] {topic}: {payload}")

    # ---------- Kwetsbare query (GEVAARLIJK) ----------
    sql = f"INSERT INTO sensor_data (topic, payload) VALUES ('{topic}', '{payload}')"
    try:
        cursor.execute(sql)
        conn.commit()
        print("[DB] Inserted payload")
    except Exception as e:
        print(f"[DB ERROR] {e}")

# ---------- MQTT client setup ----------
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect("localhost", 1883, 60)
client.loop_forever()