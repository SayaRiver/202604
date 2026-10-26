# python 3.11
#
# pubx.py  -  MQTT traffic generator (latency-measurement build)
#
# Usage:  python3 pubx.py <type> [ue_id]
#   <type>  : 1..5  (same traffic types as before)
#   [ue_id] : label for THIS ue, e.g. ue1 / ue2 (default: auto from client_id)
#
# Every payload starts with:  TS=<epoch_seconds>  SEQ=<ue_id>:<n>
#   - TS = time.time() read ONCE (epoch, timezone-free) -> subscriber just subtracts, no 7h offset
#   - SEQ carries ue_id so the subscriber can split multiple UEs apart
#
# Traffic shape (topics, QoS0/QoS2 split, timing, 800-msg cap) unchanged.
 
import random, time, sys, string
from paho.mqtt import client as mqtt_client
 
broker = '192.168.70.150'   # must be reachable via oaitun (through the RAN)
port = 1883
 
typ = sys.argv[1]
ue_id = sys.argv[2] if len(sys.argv) > 2 else None
 
client_id = f'publish-{random.randint(0, 100000)}'
if ue_id is None:
    ue_id = client_id
 
def set_topic_type(t):
    return {"1":"Temperature","2":"Temperature_Burst_messages","3":"Camera_logs",
            "4":"Humidity","5":"Camera_Burst_messages"}.get(t, "Unknown")
 
def generate_message(t, seq):
    now = time.time()
    marker = f"TS={now:.6f} SEQ={ue_id}:{seq}"
    if t == "1":
        body, ab = f"Current temperature is: {random.randint(18,28)}", False
    elif t == "2":
        temp = random.randint(-40, 80)
        if temp > 50:   body, ab = f"Alert! High temperature: {temp}", True
        elif temp < -10: body, ab = f"Alert! Low temperature: {temp}", True
        else:           body, ab = f"Current temperature is: {temp}", False
    elif t == "3":
        body, ab = "Camera log: Event detected", False
    elif t == "4":
        body, ab = f"Current humidity is: {random.randint(20,50)}%", False
    elif t == "5":
        if random.random() < 0.1:
            long_msg = ''.join(random.choices(string.ascii_letters + string.digits, k=1000))
            body, ab = f"Camera burst: High activity detected. Raw data: {long_msg}", True
        else:
            body, ab = "Camera: Normal activity", False
    else:
        body, ab = "Unknown type", False
    return f"{marker} {body}", ab
 
def connect_mqtt():
    def on_connect(c, u, f, rc):
        print("Connected to MQTT Broker!" if rc == 0 else f"Failed rc={rc}")
    c = mqtt_client.Client(client_id=client_id)
    c.on_connect = on_connect
    try:
        c.connect(broker, port)
    except Exception as e:
        print(f"Failed to connect to broker: {e}"); sys.exit(1)
    return c
 
def publish(c, topic, t):
    n = 0
    while True:
        msg, ab = generate_message(t, n)
        qos = 2 if ab else 0
        if ab and (t == "2" or t == "5"):
            for _ in range(1):
                r = c.publish(topic, msg, qos=qos)
                print(f"Send [{ue_id}] {msg[:60]}... QoS{qos}" if r[0]==0 else "Failed")
                time.sleep(1)
        else:
            r = c.publish(topic, msg, qos=qos)
            print(f"Send [{ue_id}] {msg[:70]}... QoS{qos}" if r[0]==0 else "Failed")
        n += 1
        if t in ["1","4"]:   time.sleep(0.5)
        elif t in ["2","5"]:
            if not ab:       time.sleep(3)
        else:                time.sleep(random.uniform(0.1, 0.3))
        if n >= 800: break
 
def run():
    topic = set_topic_type(typ)
    c = connect_mqtt(); c.loop_start()
    print(f"[{ue_id}] publishing type {typ} to topic '{topic}'")
    publish(c, topic, typ)
    c.loop_stop()
 
if __name__ == '__main__':
    run()
 
