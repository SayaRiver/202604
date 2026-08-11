# python 3.11

import random
import time
import datetime
import sys
import string


from paho.mqtt import client as mqtt_client

# broker = '192.168.0.216'
broker = '192.168.70.150'  # dedicated mqtt-broker service (docker-compose-with-nfapi.yaml), was colliding with vnf's own IP
port = 1883

# Check if a command line argument is provided
type = sys.argv[1]
if type == "1":
    type_int = 1
elif type == "2":
    type_int = 2
elif type == "3":
    type_int = 3
elif type == "4":
    type_int = 4
elif type == "5":
    type_int = 5

client_id = f'publish-{random.randint(0, 1000)}'

# Rest of your code remains the same...

def set_topic_type(type):
    if type == "1":
        topic_name = "Temperature"
    if type == "2":
        topic_name = "Temperature_Burst_messages"
    if type == "3":
        topic_name = "Camera_logs"
    if type == "4":
        topic_name = "Humidity"
    if type == "5":
        topic_name = "Camera_Burst_messages"
    return topic_name

def generate_message(type):
    current_time = datetime.datetime.now()
    timestamp = time.strftime('%Y-%m-%d %H:%M:%S.') + f"{current_time.microsecond:06d}"
    if type == "1":
        return f"[{timestamp}] Current temperature is: {random.randint(18, 28)}", False
    elif type == "2":
        temp = random.randint(-40, 80)
        is_abnormal = temp > 50 or temp < -10
        if temp > 50:
            return f"[{timestamp}] Alert! High temperature: {temp}", True
        elif temp < -10:
            return f"[{timestamp}] Alert! Low temperature: {temp}", True
        else:
            return f"[{timestamp}] Current temperature is: {temp}", False
    elif type == "3":
        return f"[{timestamp}] Camera log: Event detected", False
    elif type == "4":
        return f"[{timestamp}] Current humidity is: {random.randint(20, 50)}%", False
    elif type == "5":
        is_burst = random.random() < 0.1  # 10% chance of burst
        if is_burst:
            # Generate a long message for burst
            long_message = ''.join(random.choices(string.ascii_letters + string.digits, k=1000))
            return f"[{timestamp}] Camera burst: High activity detected. Raw data: {long_message}", True
        else:
            return f"[{timestamp}] Camera: Normal activity", False
    else:
        return f"[{timestamp}] Unknown type", False


def connect_mqtt():
    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            print("Connected to MQTT Broker!")
        else:
            print(f"Failed to connect, return code {rc}")

    client = mqtt_client.Client(client_id=client_id)
    client.on_connect = on_connect
    try:
        client.connect(broker, port)
    except Exception as e:
        print(f"Failed to connect to broker: {e}")
        sys.exit(1)
    return client

def publish(client, topic, type):
    msg_count = 0
    while True:
        msg, is_abnormal = generate_message(type)
        
        # Set QoS level based on whether the data is abnormal
        qos = 2 if is_abnormal else 0
        
        # For abnormal data (temperature > 50 or < -10), send multiple times every second
        if is_abnormal and (type == "2" or type == "5"):
            repeat_count = 1  # Randomly choose to repeat 3-7 times
            for _ in range(repeat_count):
                try:
                    result = client.publish(topic, msg, qos=qos)
                    status = result[0]
                    if status == 0:
                        print(f"Send `{msg[:50]}...` to topic `{topic}` with QoS {qos}")
                    else:
                        print(f"Failed to send message to topic {topic}")
                except Exception as e:
                    print(f"Error publishing message: {e}")
                time.sleep(1)  # Wait 1 second before sending the next repeat
        else:
            try:
                result = client.publish(topic, msg, qos=qos)
                status = result[0]
                if status == 0:
                    print(f"Send `{msg[:100]}...` to topic `{topic}` with QoS {qos}")
                else:
                    print(f"Failed to send message to topic {topic}")
            except Exception as e:
                print(f"Error publishing message: {e}")
        
        msg_count += 1
        
        if type in ["1", "4"]:  # Periodic output for types 1 and 4
            time.sleep(0.5)  # Output every second
        elif type in ["2", "5"]:  # Updated logic for types 2 and 5
            if not is_abnormal:
                time.sleep(3)  # Send every 3 seconds for normal data
        else:  # Continuous random output for type 3
            time.sleep(random.uniform(0.1, 0.3))
        
        if msg_count >= 800:  # Limit to 5 normal messages or abnormal sequences per run
            break
            

def run():
    topic = set_topic_type(type)
    client = connect_mqtt()
    client.loop_start()
    publish(client, topic, type)
    client.loop_stop()

if __name__ == '__main__':
    #num = 1
    #while num <= 800:
    run()
        #print(f"Run no.{num} completed")
        #num += 1
