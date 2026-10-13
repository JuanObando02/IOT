#!/usr/bin/env python3
"""Adicional 2 (WiFi): mide la latencia HTTP de N peticiones a /api del ESP32
y reporta el RSSI que informa el propio ESP32 en el JSON.

Uso: python3 latencia_wifi.py 192.168.1.50 [N]
"""
import json
import statistics
import sys
import time
import urllib.request


def main():
    if len(sys.argv) < 2:
        sys.exit("Uso: latencia_wifi.py <IP-del-ESP32> [N]")
    ip = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    url = f"http://{ip}/api"

    tiempos, rssi, fallos = [], [], 0
    for i in range(1, n + 1):
        t0 = time.perf_counter()
        try:
            with urllib.request.urlopen(url, timeout=3) as r:
                datos = json.loads(r.read())
            dt = (time.perf_counter() - t0) * 1000
            tiempos.append(dt)
            rssi.append(datos["rssi"])
            print(f"{i:2d}: {dt:7.1f} ms  RSSI={datos['rssi']} dBm")
        except Exception as e:
            fallos += 1
            print(f"{i:2d}: ERROR {e}")
        time.sleep(0.2)

    if tiempos:
        print("\n==== Resumen ====")
        print(f"Exitosas : {len(tiempos)}/{n}  (fallos: {fallos})")
        print(f"Latencia : media={statistics.mean(tiempos):.1f} ms  "
              f"min={min(tiempos):.1f}  max={max(tiempos):.1f}  "
              f"desv={statistics.pstdev(tiempos):.1f}")
        print(f"RSSI WiFi: media={statistics.mean(rssi):.1f} dBm")


if __name__ == "__main__":
    main()