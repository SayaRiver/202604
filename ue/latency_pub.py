#!/usr/bin/env python3
"""
UE-side latency probe: publishes MQTT messages carrying a send timestamp so
the broker-side subscriber (latency/latency_sub.py) can compute one-way
uplink latency = recv_ts - publish_ts.

Valid only when publisher and subscriber share a clock - true here because
every container runs on the same docker host and shares its kernel clock.

Run inside the ue / ue2 container (this directory is bind-mounted there):
    python3 latency_pub.py --ue-id 1 --period 3
    python3 latency_pub.py --ue-id 2 --period 3

--align makes every UE publish on the same wall-clock boundaries
(t mod period == 0), so UEs started at different times still transmit
simultaneously - this is the "synchronized SR" stress case from the
problem statement. Without --align each UE keeps its own phase.
"""
import argparse
import json
import sys
import time

from paho.mqtt import client as mqtt_client


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--ue-id", type=int, required=True)
    p.add_argument("--broker", default="192.168.70.150")
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--period", type=float, default=3.0, help="seconds between publishes")
    p.add_argument("--qos", type=int, default=0, choices=[0, 1, 2])
    p.add_argument("--count", type=int, default=0, help="messages to send, 0 = run until Ctrl+C")
    p.add_argument("--align", action="store_true",
                   help="publish on wall-clock multiples of --period (synchronized across UEs)")
    p.add_argument("--topic", default=None, help="default: latency/ue<ue-id>")
    args = p.parse_args()

    topic = args.topic or f"latency/ue{args.ue_id}"

    # paho-mqtt 1.x API (apt's python3-paho-mqtt is 1.6.1); also works on 2.x
    client = mqtt_client.Client(client_id=f"latency-ue{args.ue_id}")
    try:
        client.connect(args.broker, args.port)
    except Exception as e:
        print(f"Failed to connect to broker {args.broker}:{args.port}: {e}", file=sys.stderr)
        sys.exit(1)
    client.loop_start()

    print(f"[ue{args.ue_id}] publishing to {topic} every {args.period}s "
          f"(qos={args.qos}, align={args.align})")

    seq = 0
    try:
        while args.count == 0 or seq < args.count:
            if args.align:
                now = time.time()
                next_slot = (int(now / args.period) + 1) * args.period
                time.sleep(max(0.0, next_slot - time.time()))
            publish_ts = time.time()
            payload = json.dumps({"ue_id": args.ue_id, "seq": seq, "publish_ts": publish_ts})
            client.publish(topic, payload, qos=args.qos)
            print(f"[ue{args.ue_id}] seq={seq} publish_ts={publish_ts:.6f}")
            seq += 1
            if not args.align:
                time.sleep(args.period)
    except KeyboardInterrupt:
        pass
    finally:
        client.loop_stop()
        client.disconnect()
        print(f"[ue{args.ue_id}] sent {seq} messages")


if __name__ == "__main__":
    main()
