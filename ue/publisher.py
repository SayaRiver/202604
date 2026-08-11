import paho.mqtt.client as mqtt
import time, json

BROKER_IP = "192.168.70.150"  # dedicated mqtt-broker service (docker-compose-with-nfapi.yaml)
BROKER_PORT = 1883
TOPIC = "iiot/ue1/sensor"

client = mqtt.Client(client_id="UE1")
client.connect(BROKER_IP, BROKER_PORT)

while True:
    ts = time.time()
    payload = json.dumps({"ue_id": 1, "publish_ts": ts})
    client.publish(TOPIC, payload, qos=1)
    print(f"[PUB] {ts:.6f}")
    time.sleep(1.0)
