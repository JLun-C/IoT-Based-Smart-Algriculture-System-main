import serial
import json
from datetime import datetime
# pip install pymongo
# from pymongo import MongoClient
import psycopg2
from psycopg2 import extras

# # ===== SERIAL CONFIG =====
# SERIAL_PORT = "COM12"   # change to your port
# BAUD_RATE = 115200

# ===== SUPABASE CONFIG =====
DB_CONFIG = {
    'user': "postgres.eeiazmvadvbflsuulcpz",
    'password': "357IIooTT202526^^",
    'host': "aws-1-ap-south-1.pooler.supabase.com",
    'port': "6543",
    'dbname': "postgres"
}

# # ===== CONNECT SERIAL =====
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print(f"--- Serial Connected to {SERIAL_PORT} ---")
except Exception as e:
    print(f"Serial Connection Error: {e}")
    exit()

# client = MongoClient(MONGO_URI)
# 
# db = client["iot_database"]
# collection = db["sensor_data"]
# 
# print(" Connected to ESP32 & MongoDB")

# ===== CONNECT SUPABASE =====
try:
    conn = psycopg2.connect(**DB_CONFIG)
    # Use autocommit to ensure data is saved immediately
    conn.automatic = True
    cursor = conn.cursor()
    print("--- Connected to Supabase PostgreSQL ---")
except Exception as e:
    print(f"Supabase Connection Error: {e}")
    exit()

base_dir = os.path.dirname(os.path.abspath(__file__))
file_path = os.path.join(base_dir, 'sensor_data.json')

if os.path.exists(file_path):
    print(f"Found dummy file at: {file_path}")
    with open(file_path, 'r') as f:
        try:
            dummy_data = json.load(f)
            print(f"Dummy Data Loaded: {dummy_data}")
        except json.JSONDecodeError as e:
            print(f"Error decoding JSON from dummy file: {e}")

while True:
    try:
        line = ser.readline().decode('utf-8').strip()
        if line:
            data = json.loads(line)  # Parse JSON from ESP32

            temp = data.get('Temperature', None)
            humidity = data.get('Humidity', None)
            soil_moisture = data.get('Soil_Moisture', None)
            is_raining = data.get('Is_Raining', None)
            pump = data.get('Pump', None)
            timestamp = datetime.now()

            # Insert data into Supabase PostgreSQL
            insert_query = """INSERT INTO sensor_data (timestamp, temperature, humidity, soil_moisture, is_raining, pump)
                              VALUES (%s, %s, %s, %s, %s, %s);"""
            cursor.execute(insert_query, (timestamp, temp, humidity, soil_moisture, is_raining, pump))
            print(f"Inserted into Supabase: {data}")

    except json.JSONDecodeError: 
        print("JSON Decode Error: Invalid JSON format")
    except Exception as e:
        print(f"Upload Error: {e}")


