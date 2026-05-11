from flask import Flask, request, jsonify, session
from flask_cors import CORS

app = Flask(__name__)
app.secret_key = "super_secret_key_123" # Required for sessions
CORS(app) # Allows your frontend to talk to this server

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
    # In a real setup, this would fetch from the ESP32 or a database
    import random
    return jsonify({"humidity": random.uniform(30.0, 60.0)})

if __name__ == '__main__':
    # Running on 0.0.0.0 makes it accessible outside the container
    app.run(host='0.0.0.0', port=5000)