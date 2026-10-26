
#!/usr/bin/env python3
#
# sub_latency.py  -  latency subscriber (multi-UE aware)
#
# Usage:  python3 sub_latency.py <N_label>
#   e.g.  python3 sub_latency.py 1     -> writes latency_N1.csv
#         python3 sub_latency.py 2     -> writes latency_N2.csv
#
# Reads the "TS=<epoch> SEQ=<ue_id>:<n>" marker that pubx.py puts at the start of
# each payload. Latency = recv_time - TS, in ms. Because TS is epoch (timezone-free)
# and all containers share the host clock, no offset correction is needed.
#
# The ue_id column lets you split N=2+ data per UE afterwards.
#
# Subscribes to "#" (all topics) so it catches every UE regardless of topic.
# Deps: pip install paho-mqtt
 
import paho.mqtt.client as mqtt
import time, csv, re, sys
 
BROKER = "192.168.70.150"; PORT = 1883
N = sys.argv[1] if len(sys.argv) > 1 else "X"
OUT = f"latency_N{N}.csv"
 
pat = re.compile(r"TS=([0-9.]+)\s+SEQ=([^:\s]+):(\d+)")
 
f = open(OUT, "w", newline=""); w = csv.writer(f)
w.writerow(["ue_id","seq","send_ts","recv_ts","latency_ms"]); f.flush()
 
count = {}
def on_message(c, u, msg):
    recv = time.time()
    m = pat.search(msg.payload.decode(errors="replace"))
    if not m: return
    send_ts = float(m.group(1)); ue = m.group(2); seq = m.group(3)
    lat = (recv - send_ts) * 1000.0
    w.writerow([ue, seq, f"{send_ts:.6f}", f"{recv:.6f}", f"{lat:.3f}"]); f.flush()
    count[ue] = count.get(ue, 0) + 1
    print(f"[{ue}] {lat:8.2f} ms   (total {ue}: {count[ue]})")
 
c = mqtt.Client(); c.on_message = on_message
c.connect(BROKER, PORT); c.subscribe("#")
print(f"subscribed to # on {BROKER}, writing {OUT}  (Ctrl+C to stop)")
try:
    c.loop_forever()
except KeyboardInterrupt:
    print(f"\nstopped. per-UE counts: {count}")
    f.close()
 
