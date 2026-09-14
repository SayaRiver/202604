#!/usr/bin/env python3
"""
Broker-side latency recorder: subscribes to latency/# and logs one row per
received message with the one-way uplink latency (recv_ts - publish_ts).

Run on the docker HOST (it reaches the broker on demo-oai-public-net directly
and shares the same kernel clock as the UE containers):
    python3 latency_sub.py --out run_N2.csv

Requires: pip install paho-mqtt
"""
import argparse
import csv
import json
import sys
import time

from paho.mqtt import client as mqtt_client


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--broker", default="192.168.70.150")
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--topic", default="latency/#")
    p.add_argument("--out", required=True, help="CSV file to append to")
    args = p.parse_args()

    f = open(args.out, "a", newline="")
    w = csv.writer(f)
    if f.tell() == 0:
        w.writerow(["recv_ts", "ue_id", "seq", "publish_ts", "latency_ms", "topic"])
        f.flush()

    stats = {"n": 0}

    def on_message(client, userdata, msg):
        recv_ts = time.time()
        try:
            d = json.loads(msg.payload.decode("utf-8"))
            latency_ms = (recv_ts - float(d["publish_ts"])) * 1000.0
            w.writerow([f"{recv_ts:.6f}", d["ue_id"], d["seq"], f"{d['publish_ts']:.6f}",
                        f"{latency_ms:.3f}", msg.topic])
            f.flush()
            stats["n"] += 1
            print(f"ue{d['ue_id']} seq={d['seq']} latency={latency_ms:.2f} ms  (total {stats['n']})")
        except Exception as e:
            print(f"bad payload on {msg.topic}: {e}", file=sys.stderr)

    # on_connect signature differs between paho 1.x and 2.x - accept anything
    def on_connect(client, userdata, flags, rc, *extra):
        print(f"connected to {args.broker}:{args.port} (rc={rc}), subscribing {args.topic}")
        client.subscribe(args.topic, qos=2)

    client = mqtt_client.Client(client_id="latency-sub")
    client.on_connect = on_connect
    client.on_message = on_message
    try:
        client.connect(args.broker, args.port)
    except Exception as e:
        print(f"Failed to connect to broker: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"logging to {args.out} (Ctrl+C to stop)")
    try:
        client.loop_forever()
    except KeyboardInterrupt:
        pass
    finally:
        f.close()
        print(f"wrote {stats['n']} rows to {args.out}")


if __name__ == "__main__":
    main()
