from flask import Flask, request, jsonify, session
from flask_cors import CORS
import requests

app = Flask(__name__)
app.secret_key = "super_secret_key_123" # Required for sessions
CORS(app) # Allows your frontend to talk to this server

ESP32_IP = "192.168.1.182"

# Mock Database
USERS = {
    "admin": "esp32_secret"
}

@app.route('/api/login', methods=['POST'])
def login():
    data = request.json
    username = data.get('username')
    password = data.get('password')
    
    if USERS.get(username) == password:
        session['user'] = username
        return jsonify({"status": "success", "message": "Logged in"}), 200
    
    return jsonify({"status": "error", "message": "Unauthorized"}), 401

@app.route('/api/humidity', methods=['GET'])
def get_humidity():
    try:
        # Flask requests the data from the hardware where CORS rules don't exist
        response = requests.get(f"http://{ESP32_IP}/api/humidity", timeout=3)
        if response.status_code == 200:
            return jsonify(response.json())
        return jsonify({"status": "error", "message": "ESP32 returned an error status"}), 502
    except requests.exceptions.RequestException as e:
        # If the ESP32 drops the socket, fail gracefully without throwing console errors
        print(f"Failed to connect to ESP32: {e}")
        return jsonify({"status": "error", "message": "ESP32 unreachable"}), 503

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5001)