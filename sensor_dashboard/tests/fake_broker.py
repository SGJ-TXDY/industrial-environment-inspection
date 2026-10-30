#!/usr/bin/env python3
"""Minimal MQTT 3.1.1 broker for host tests (stdlib only, not for real use).

Answers CONNECT/PUBLISH/PINGREQ/DISCONNECT and appends every PUBLISH as a
JSON line to --log. --delay keeps the port closed at first so the client's
offline backlog and reconnect can be tested; --password makes CONNACK
refuse other passwords with code 4.
"""
import argparse
import json
import socket
import threading
import time


def read_exact(sock, n):
    data = b""
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError
        data += chunk
    return data


def read_packet(sock):
    first = read_exact(sock, 1)[0]
    length, shift = 0, 0
    while True:
        b = read_exact(sock, 1)[0]
        length |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            break
    return first, read_exact(sock, length)


def string(body, pos):
    n = int.from_bytes(body[pos:pos + 2], "big")
    return body[pos + 2:pos + 2 + n], pos + 2 + n


def connack_code(body, password):
    _, pos = string(body, 0)          # protocol name
    flags = body[pos + 1]
    pos += 4                          # level, flags, keepalive
    _, pos = string(body, pos)        # client id
    if flags & 0x04:                  # will topic + message
        _, pos = string(body, pos)
        _, pos = string(body, pos)
    if flags & 0x80:
        _, pos = string(body, pos)
    given = string(body, pos)[0].decode() if flags & 0x40 else None
    return 0 if password is None or given == password else 4


def serve(conn, args, lock):
    try:
        while True:
            first, body = read_packet(conn)
            kind = first >> 4
            if kind == 1:
                code = connack_code(body, args.password)
                conn.sendall(bytes([0x20, 2, 0, code]))
                if code:
                    return
            elif kind == 3:
                qos = (first >> 1) & 3
                topic, pos = string(body, 0)
                if qos:
                    packet_id = body[pos:pos + 2]
                    pos += 2
                    conn.sendall(bytes([0x40, 2]) + packet_id)
                with lock, open(args.log, "a", encoding="utf-8") as f:
                    f.write(json.dumps({"topic": topic.decode(), "payload": body[pos:].decode(),
                                        "qos": qos, "retain": first & 1}) + "\n")
            elif kind == 12:
                conn.sendall(bytes([0xD0, 0]))
            elif kind == 14:
                return
    except (ConnectionError, OSError):
        pass
    finally:
        conn.close()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, required=True)
    p.add_argument("--log", required=True)
    p.add_argument("--delay", type=float, default=0)
    p.add_argument("--password")
    p.add_argument("--duration", type=float, default=60)
    args = p.parse_args()
    open(args.log, "w").close()
    time.sleep(args.delay)
    lock = threading.Lock()
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", args.port))
    server.listen(4)
    server.settimeout(0.5)
    end = time.time() + args.duration
    while time.time() < end:
        try:
            conn, _ = server.accept()
        except socket.timeout:
            continue
        threading.Thread(target=serve, args=(conn, args, lock), daemon=True).start()


if __name__ == "__main__":
    main()
