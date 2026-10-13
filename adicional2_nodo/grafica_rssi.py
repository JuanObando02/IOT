#!/usr/bin/env python3
"""Adicional 2: gráfica de RSSI vs. distancia (WiFi y BLE en la misma figura).
Lee mediciones.csv (reemplace los valores de ejemplo por sus mediciones).

Uso: python3 grafica_rssi.py [mediciones.csv]
"""
import csv
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

archivo = sys.argv[1] if len(sys.argv) > 1 else "mediciones.csv"
d, wifi, ble = [], [], []
with open(archivo, newline="") as f:
    for fila in csv.DictReader(f):
        d.append(float(fila["distancia_m"]))
        wifi.append(float(fila["rssi_wifi_dbm"]))
        ble.append(float(fila["rssi_ble_dbm"]))

plt.figure(figsize=(7, 4.5))
plt.plot(d, wifi, "o-", label="WiFi")
plt.plot(d, ble, "s-", label="BLE")
for y, txt in [(-50, "Excelente"), (-67, "Buena"), (-80, "Regular")]:
    plt.axhline(y, color="gray", linestyle=":", linewidth=0.8)
    plt.text(max(d), y + 0.5, txt, ha="right", fontsize=8, color="gray")
plt.xlabel("Distancia [m]")
plt.ylabel("RSSI [dBm]")
plt.title("RSSI vs. distancia")
plt.grid(alpha=0.3)
plt.legend()
plt.tight_layout()
plt.savefig("rssi_vs_distancia.png", dpi=150)
print("Guardado: rssi_vs_distancia.png")