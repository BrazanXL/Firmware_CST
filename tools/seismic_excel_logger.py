#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Script de Captura y Exportación Automática a Excel con Gráficas — Mesa Sísmica CUC
===================================================================================
- Se conecta al puerto serie del ESP32 (ej. COM3 / COM4 en Windows o /dev/ttyUSB0 en Linux).
- Permite enviar comandos (<START>, <STOP>, <SET:FREQ:x>, <SET:AMP:y>, <SET:DUR:z>, <SCAN>)
  o simplemente escuchar cuando el ensayo se inicia desde los botones físicos de la mesa.
- Al recibir la trama entre `$SEISMIC_BEGIN` y `$SEISMIC_END`, construye automáticamente
  un archivo Excel (.xlsx) con:
    1. Hoja `Datos_Sismo`: Tabla completa de muestras de todos los sensores detectados (1 a 6)
       y resumen metrológico (Frecuencia, Amplitud, Duración, PGA en g y m/s²).
    2. Hoja `Graficas_Sismo`: Gráficas nativas de Excel listas para insertar en la tesis IEEE:
       - Gráfica 1: Posición de Consigna de la Mesa (mm) vs. Tiempo (ms).
       - Gráfica 2: Aceleración Eje X (g) de todos los sensores detectados (S1..S6) vs. Tiempo (ms).
"""

import argparse
import datetime
import os
import sys
import threading
import serial
import serial.tools.list_ports

try:
    import xlsxwriter
except ImportError:
    xlsxwriter = None


def parse_metadata(line: str) -> dict:
    meta = {}
    parts = line.strip().split(",")
    for p in parts[1:]:
        if "=" in p:
            k, v = p.split("=", 1)
            meta[k.strip()] = v.strip()
    return meta


def export_to_excel(headers: list, rows: list, metadata: dict, output_dir: str = "ensayos_excel") -> str:
    os.makedirs(output_dir, exist_ok=True)
    ts_str = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    freq = metadata.get("FREQ_HZ", "NA")
    amp = metadata.get("AMP_MM", "NA")

    if xlsxwriter is None:
        # Respaldo en CSV si aún no se ha instalado xlsxwriter
        csv_path = os.path.join(output_dir, f"Sismo_F{freq}Hz_A{amp}mm_{ts_str}.csv")
        with open(csv_path, "w", encoding="utf-8") as f:
            f.write(",".join(headers) + "\n")
            for r in rows:
                f.write(",".join(str(x) for x in r) + "\n")
        print(f"[AVISO] 'xlsxwriter' no instalado. Se guardó CSV en: {csv_path}")
        print("        Instala con: pip install xlsxwriter pyserial")
        return csv_path

    xlsx_path = os.path.join(output_dir, f"Sismo_F{freq}Hz_A{amp}mm_{ts_str}.xlsx")
    wb = xlsxwriter.Workbook(xlsx_path)

    # Formatos de estilo académico / IEEE
    fmt_title = wb.add_format({"bold": True, "font_size": 12, "bg_color": "#1F4E78", "font_color": "white"})
    fmt_hdr = wb.add_format({"bold": True, "bg_color": "#D9E1F2", "border": 1, "align": "center"})
    fmt_num = wb.add_format({"num_format": "0.0000", "border": 1})
    fmt_int = wb.add_format({"num_format": "0", "border": 1, "align": "center"})
    fmt_meta_k = wb.add_format({"bold": True, "bg_color": "#F2F2F2", "border": 1})
    fmt_meta_v = wb.add_format({"border": 1, "align": "center"})

    ws_data = wb.add_worksheet("Datos_Sismo")
    ws_charts = wb.add_worksheet("Graficas_Sismo")

    # 1. Bloque de metadatos en las primeras filas
    ws_data.merge_range(0, 0, 0, max(len(headers) - 1, 5),
                        "REGISTRO EXPERIMENTAL MESA SÍSMICA — UNIVERSIDAD DE LA COSTA (CUC)", fmt_title)

    meta_items = [
        ("Frecuencia Ordenada (Hz)", metadata.get("FREQ_HZ", "-")),
        ("Amplitud Ordenada (mm)", metadata.get("AMP_MM", "-")),
        ("Duración (s)", metadata.get("DUR_S", "-")),
        ("Sensores Detectados", metadata.get("ACTIVE_COUNT", "-")),
        ("Máscara TCA9548A", metadata.get("ACTIVE_MASK", "-")),
        ("PGA Mesa Medido (g)", metadata.get("PGA_MESA_G", "-")),
    ]
    for idx, (k, v) in enumerate(meta_items):
        col_offset = (idx % 3) * 2
        row_offset = 1 + (idx // 3)
        ws_data.write(row_offset, col_offset, k, fmt_meta_k)
        ws_data.write(row_offset, col_offset + 1, v, fmt_meta_v)

    start_row = 4
    for col_idx, h in enumerate(headers):
        ws_data.write(start_row, col_idx, h, fmt_hdr)
        ws_data.set_column(col_idx, col_idx, 15)

    for row_idx, r in enumerate(rows):
        excel_row = start_row + 1 + row_idx
        for col_idx, val in enumerate(r):
            if col_idx == 0:
                ws_data.write_number(excel_row, col_idx, int(float(val)), fmt_int)
            else:
                ws_data.write_number(excel_row, col_idx, float(val), fmt_num)

    last_row = start_row + len(rows)

    if len(rows) > 1:
        # Gráfica 1: Consigna de Desplazamiento (mm) vs Tiempo (ms)
        chart_pos = wb.add_chart({"type": "scatter", "subtype": "smooth"})
        chart_pos.add_series({
            "name": ["Datos_Sismo", start_row, 1],
            "categories": ["Datos_Sismo", start_row + 1, 0, last_row, 0],
            "values": ["Datos_Sismo", start_row + 1, 1, last_row, 1],
            "line": {"color": "#1F4E78", "width": 1.75},
        })
        chart_pos.set_title({"name": f"Desplazamiento de Consigna de la Mesa (F={freq} Hz, A={amp} mm)"})
        chart_pos.set_x_axis({"name": "Tiempo (ms)"})
        chart_pos.set_y_axis({"name": "Posición (mm)"})
        chart_pos.set_size({"width": 780, "height": 340})
        ws_charts.insert_chart("B2", chart_pos)

        # Gráfica 2: Respuesta de Aceleración Eje X (g) de todos los sensores activos (S1..S6)
        chart_acc = wb.add_chart({"type": "scatter", "subtype": "straight"})
        palette = ["#C00000", "#0070C0", "#00B050", "#7030A0", "#ED7D31", "#44546A"]
        series_added = 0
        for col_idx, h in enumerate(headers):
            if h.endswith("_ax_g"):
                chart_acc.add_series({
                    "name": ["Datos_Sismo", start_row, col_idx],
                    "categories": ["Datos_Sismo", start_row + 1, 0, last_row, 0],
                    "values": ["Datos_Sismo", start_row + 1, col_idx, last_row, col_idx],
                    "line": {"color": palette[series_added % len(palette)], "width": 1.25},
                })
                series_added += 1

        if series_added > 0:
            chart_acc.set_title({"name": "Respuesta de Aceleración Sísmica por Nivel (Eje X en g)"})
            chart_acc.set_x_axis({"name": "Tiempo (ms)"})
            chart_acc.set_y_axis({"name": "Aceleración (g)"})
            chart_acc.set_size({"width": 780, "height": 380})
            ws_charts.insert_chart("B20", chart_acc)

    wb.close()
    print(f"\n[OK] Archivo Excel con gráficas generado exitosamente:\n     -> {os.path.abspath(xlsx_path)}\n")
    return xlsx_path


def interactive_sender(ser: serial.Serial):
    print("Comandos disponibles: <START>, <STOP>, <SCAN>, <STATUS>, <SET:FREQ:2.5>, <SET:AMP:5.0>, <SET:DUR:10.0>")
    while ser.is_open:
        try:
            cmd = sys.stdin.readline()
            if not cmd:
                break
            cmd = cmd.strip()
            if cmd:
                if not cmd.startswith("<"):
                    cmd = f"<{cmd}>"
                ser.write((cmd + "\r\n").encode("utf-8"))
        except Exception:
            break


def main():
    parser = argparse.ArgumentParser(description="Capturador Serial a Excel con Gráficas — Mesa Sísmica CUC")
    parser.add_argument("--port", "-p", help="Puerto serial (ej. COM3 o /dev/ttyUSB0)")
    parser.add_argument("--baud", "-b", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--out", "-o", default="ensayos_excel", help="Carpeta de salida para los archivos .xlsx")
    args = parser.parse_args()

    port = args.port
    if not port:
        ports = list(serial.tools.list_ports.comports())
        if not ports:
            print("[ERROR] No se detectaron puertos seriales conectados. Usa --port COMx")
            sys.exit(1)
        port = ports[0].device
        print(f"[INFO] Usando puerto detectado automáticamente: {port}")

    ser = serial.Serial(port, args.baud, timeout=0.2)
    print(f"[CONECTADO] Escuchando tramas sísmicas en {port} @ {args.baud} baudios...")

    t = threading.Thread(target=interactive_sender, args=(ser,), daemon=True)
    t.start()

    capturing = False
    headers = []
    rows = []
    metadata = {}

    try:
        while True:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue

            if line.startswith("$SEISMIC_BEGIN"):
                capturing = True
                headers = []
                rows = []
                metadata = parse_metadata(line)
                print(f"\n>>> Inicio de ensayo sísmico detectado: {metadata}")
            elif line.startswith("$CSV_HDR,") and capturing:
                headers = line.split(",")[1:]
                print(f"    Columnas activas ({len(headers)}): {', '.join(headers)}")
            elif line.startswith("$DATA,") and capturing:
                vals = line.split(",")[1:]
                rows.append(vals)
                if len(rows) % 50 == 0:
                    print(f"    Muestras recibidas: {len(rows)}...", end="\r")
            elif line.startswith("$SEISMIC_END") and capturing:
                end_meta = parse_metadata(line)
                metadata.update(end_meta)
                capturing = false_flag = False
                print(f"\n<<< Fin de ensayo ({len(rows)} muestras). Generando Excel con gráficas...")
                export_to_excel(headers, rows, metadata, args.out)
            else:
                print(f"[ESP32] {line}")
    except KeyboardInterrupt:
        print("\nCerrando monitor...")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
