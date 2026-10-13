"""Mide ESP-NOW: abre el puerto, dispara la prueba y extrae el RESUMEN.

Uso: py medir_espnow.py [COM5] [timeout_s]
Imprime tasa de entrega (%), latencia media (ms), envios fallidos y el resumen crudo.
"""
import re
import sys
import time

import serial

PUERTO = sys.argv[1] if len(sys.argv) > 1 else "COM5"
TIMEOUT = float(sys.argv[2]) if len(sys.argv) > 2 else 180.0


def numero(patron, texto):
    m = re.search(patron, texto, re.IGNORECASE)
    return float(m.group(1).replace(",", ".")) if m else None


def main():
    try:
        ser = serial.Serial(PUERTO, 115200, timeout=0.2)
    except serial.SerialException as e:
        sys.exit(f"No se pudo abrir {PUERTO} (¿monitor abierto o desconectado?): {e}")

    buf = ""
    enviado = False
    inicio = time.time()
    with ser:
        while time.time() - inicio < TIMEOUT:
            buf += ser.read(4096).decode("utf-8", errors="replace")
            # si el nodo ya estaba esperando, el mensaje no se reimprime: enviar igual tras 4 s
            if not enviado and (re.search(r"escriba cualquier car", buf, re.IGNORECASE)
                                or time.time() - inicio > 4):
                ser.write(b"x")
                enviado = True
                buf_inicio = len(buf)
            # resumen: "======== RESUMEN ========" ... linea de "=" de cierre
            m = re.search(r"=+ RESUMEN =+(.*?)\n={10,}", buf, re.DOTALL)
            if enviado and m:
                break
        else:
            print(buf)
            sys.exit("Timeout: no llegó el RESUMEN" + ("" if enviado else " (nunca apareció 'escriba cualquier caracter')"))

    resumen = m.group(0)
    tasa = numero(r"Tasa de entrega\D*([\d.,]+)", resumen)
    lat = numero(r"Latencia media[^(]*\(\s*([\d.,]+)\s*ms\)", resumen)
    fallos = numero(r"Fallidos\D*(\d+)", resumen)
    print(resumen)
    print("\n--- Resultado ---")
    print(f"Tasa de entrega: {tasa} %")
    print(f"Latencia media : {lat} ms (hasta ACK de capa MAC)")
    print(f"Envíos fallidos: {fallos}")
    if tasa is not None and tasa < 90:
        print("ATENCIÓN: tasa de entrega por debajo del 90 %")


if __name__ == "__main__":
    main()
