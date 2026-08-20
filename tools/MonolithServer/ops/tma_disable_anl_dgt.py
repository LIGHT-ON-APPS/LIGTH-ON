"""TMA의 아날로그·디지털 수집을 끈다.

브로커 listener가 websockets 전용(9001)이라 mosquitto_pub으로는 발행할 수 없다.
collector 이미지에 들어 있는 paho-mqtt로 같은 경로를 쓴다.

    docker exec monolith-v2-collector-1 python /tmp/tma_disable_anl_dgt.py
    docker exec monolith-v2-collector-1 python /tmp/tma_disable_anl_dgt.py --reboot

payload는 반드시 바이너리 0x00이다. 펌웨어가 `evt->data[0] != 0`으로만 보므로
문자열 "0"(0x30)을 보내면 오히려 켜진다(mqtt.c의 set 핸들러).

NVS에만 쓰고 다음 부팅부터 적용된다. --reboot을 주면 `<ch>/cmd/rbt`로 원격
재부팅까지 한다(esp_restart).
"""

from __future__ import annotations

import os
import sys
import threading
import time

import paho.mqtt.client as mqtt

CHANNEL = os.environ.get("MQTT_CHANNEL", "lighton")
HOST = os.environ.get("MQTT_HOST", "mosquitto")
PORT = int(os.environ.get("MQTT_PORT", "9001"))
KEY = os.environ["MQTT_KEY"]

SET_TOPICS = [f"{CHANNEL}/set/anl/en", f"{CHANNEL}/set/dgt/en"]
ACK_TOPIC = f"{CHANNEL}/ack/set"
BOOT_TOPIC = f"{CHANNEL}/d/boot"
REBOOT_TOPIC = f"{CHANNEL}/cmd/rbt"

acks: list[bytes] = []
boot_seen = threading.Event()
boot_payload: list[bytes] = []
connected = threading.Event()


def on_connect(client, userdata, flags, rc, properties=None):
    if rc != 0:
        print(f"연결 실패 rc={rc}", file=sys.stderr)
        return
    client.subscribe(ACK_TOPIC, qos=1)
    client.subscribe(BOOT_TOPIC, qos=1)  # retained LWT로 온라인 여부를 본다
    connected.set()


def on_message(client, userdata, msg):
    if msg.topic == ACK_TOPIC:
        acks.append(msg.payload)
        print(f"ack: {msg.payload!r}")
    elif msg.topic == BOOT_TOPIC:
        boot_payload.append(msg.payload)
        boot_seen.set()


def main() -> int:
    force = "--force" in sys.argv
    reboot = "--reboot" in sys.argv

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, transport="websockets")
    client.username_pw_set(CHANNEL, KEY)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(HOST, PORT, keepalive=30)
    client.loop_start()

    if not connected.wait(10):
        print("브로커 연결 시간초과", file=sys.stderr)
        return 1

    # retained boot 토픽이 'OFFLINE'이면 장치가 붙어 있지 않다. set은 QoS 0/1
    # 어느 쪽이든 오프라인 장치에는 남지 않으므로 그냥 버려진다.
    boot_seen.wait(3)
    if boot_payload and boot_payload[0] == b"OFFLINE" and not force:
        print("장치가 오프라인이다(retained boot=OFFLINE). 발행하지 않았다.", file=sys.stderr)
        print("온라인 상태에서 다시 실행하거나 --force로 강행한다.", file=sys.stderr)
        client.loop_stop()
        return 2

    for topic in SET_TOPICS:
        info = client.publish(topic, b"\x00", qos=1, retain=False)
        info.wait_for_publish(timeout=10)
        print(f"발행 {topic} <- 0x00")

    deadline = time.time() + 10
    while len(acks) < len(SET_TOPICS) and time.time() < deadline:
        time.sleep(0.2)

    if len(acks) < len(SET_TOPICS):
        print(f"ack {len(acks)}/{len(SET_TOPICS)}만 받았다. 장치 상태를 확인한다.", file=sys.stderr)
        client.loop_stop()
        return 3

    print("NVS 반영 완료. 다음 부팅부터 적용된다.")

    if reboot:
        client.publish(REBOOT_TOPIC, b"1", qos=1).wait_for_publish(timeout=10)
        print(f"발행 {REBOOT_TOPIC} (원격 재부팅). ack는 오지 않는다.")

    client.loop_stop()
    client.disconnect()
    return 0


if __name__ == "__main__":
    sys.exit(main())
