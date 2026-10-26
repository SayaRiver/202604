import paho.mqtt.client as mqtt
import time, csv, re
from datetime import datetime

BROKER = "192.168.70.150"; PORT = 1883
TOPIC = "Temperature"
OUT = "latency_N1.csv"

f = open(OUT, "w", newline=""); w = csv.writer(f)
w.writerow(["send_ts","recv_ts","latency_ms"])

pat = re.compile(r"\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\]")

def on_message(c,u,msg):
    recv = time.time()
    text = msg.payload.decode(errors="replace")
    m = pat.search(text)
    if not m: return
    send_dt = datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f")
    send_ts = send_dt.timestamp()
    #lat = (recv - send_ts)*1000.0
    lat = (recv - send_ts)*1000.0 - 25200000.0   # 减掉 7 小时固定偏移(B方案临时)
    w.writerow([f"{send_ts:.6f}", f"{recv:.6f}", f"{lat:.3f}"]); f.flush()
    print(f"{lat:.2f} ms")

c = mqtt.Client(); c.on_message = on_message
c.connect(BROKER, PORT); c.subscribe(TOPIC)
print(f"subscribed {TOPIC}@{BROKER}, writing {OUT}"); c.loop_forever()
