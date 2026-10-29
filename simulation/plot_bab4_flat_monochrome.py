#!/usr/bin/env python3
# OculoPulse Power Budget & LiPo 350 mAh Discharge Curve Generator

import os
import csv
import numpy as np
import matplotlib.pyplot as plt

plt.style.use("default")

SIM_DIR  = os.path.dirname(os.path.abspath(__file__))
BASE_DIR = os.path.dirname(SIM_DIR)

FIG_DIRS = [os.path.join(BASE_DIR, "figures")]
if os.path.exists(os.path.join(BASE_DIR, "report")):
    FIG_DIRS.append(os.path.join(BASE_DIR, "report", "figures"))
for d in FIG_DIRS:
    os.makedirs(d, exist_ok=True)


def generate_bab4_power_data_and_plot():
    subsystems = [
        "MCU ESP32-S3\n+ BLE Burst 1Hz",
        "Kamera OV2640\n(ROI 15 FPS)",
        "LED NIR 850nm\n(Strobe 12%)",
        "Haptik LRA\n(Siaga/Periodik)",
        "IMU 6-Axis\nLSM6DS3TR-C",
        "Sensor Cahaya\nVEML7700"
    ]
    currents_ma = np.array([38.00, 12.50, 2.40, 0.45, 0.25, 0.045])
    total_3v3_ma = float(np.sum(currents_ma))

    # TI TPS62740 buck efficiency (92% @ 3.3V from 3.7V LiPo)
    v_dd = 3.30
    v_bat_nom = 3.70
    eta_buck = 0.92
    i_bat_active_ma = (v_dd * total_3v3_ma) / (v_bat_nom * eta_buck)
    i_bat_daily_ma  = 0.85 * i_bat_active_ma + 0.15 * 0.12
    i_bat_wifi_ma   = 205.70

    cap_mah = 350.0
    t_hours = np.linspace(0, 8.5, 400)

    def lipo_voltage_curve(t_arr, i_draw_ma):
        soc = 1.0 - (t_arr * i_draw_ma) / cap_mah
        v = np.zeros_like(soc)
        for idx, s in enumerate(soc):
            if s <= 0.0:
                v[idx] = np.nan
            elif s > 0.15:
                v[idx] = 3.52 + 0.52 * s + 0.16 * (s ** 3)
            else:
                v[idx] = 3.00 + (3.60 - 3.00) * (s / 0.15) ** 0.75
        return v

    v_daily  = lipo_voltage_curve(t_hours, i_bat_daily_ma)
    v_active = lipo_voltage_curve(t_hours, i_bat_active_ma)
    v_wifi   = lipo_voltage_curve(t_hours, i_bat_wifi_ma)

    csv_out = os.path.join(SIM_DIR, "power_discharge_curve_350mah.csv")
    with open(csv_out, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["time_hours", "v_daily_85pct_v", "v_active_continuous_v", "v_wifi_streaming_v"])
        for idx in range(0, len(t_hours), 4):
            writer.writerow([
                f"{t_hours[idx]:.2f}",
                f"{v_daily[idx]:.3f}" if not np.isnan(v_daily[idx]) else "",
                f"{v_active[idx]:.3f}" if not np.isnan(v_active[idx]) else "",
                f"{v_wifi[idx]:.3f}" if not np.isnan(v_wifi[idx]) else ""
            ])

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10.5, 4.4), dpi=300)

    y_pos = np.arange(len(subsystems))
    bars = ax1.barh(
        y_pos, currents_ma,
        color="#dddddd", edgecolor="#111111", linewidth=1.0, height=0.58
    )
    ax1.set_yticks(y_pos)
    ax1.set_yticklabels(subsystems, fontsize=8.5)
    ax1.invert_yaxis()
    ax1.set_xlabel("Konsumsi Arus Rata-Rata pada Rel 3,3 V (mA)")
    ax1.set_xlim(0, 46)
    ax1.grid(True, axis="x", linestyle=":", color="#cccccc", alpha=0.8)

    for bar, val in zip(bars, currents_ma):
        lbl = f"{val:.2f} mA" if val >= 0.1 else f"{val:.3f} mA"
        ax1.text(
            bar.get_width() + 0.8,
            bar.get_y() + bar.get_height() / 2.0,
            lbl,
            va="center", ha="left", fontsize=8.0, color="#111111"
        )

    ax2.plot(
        t_hours, v_daily,
        color="#111111", linestyle="-", linewidth=1.5,
        label="Mode Kerja Harian + Auto-Sleep (44,2 mA -> 7,91 Jam)"
    )
    ax2.plot(
        t_hours, v_active,
        color="#444444", linestyle="--", linewidth=1.4,
        label="Mode Aktif Kontinu OculoPulse (52,0 mA -> 6,73 Jam)"
    )
    ax2.plot(
        t_hours, v_wifi,
        color="#777777", linestyle=":", linewidth=1.5,
        label="Pembanding: Streaming Video Wi-Fi (205,7 mA -> 1,70 Jam)"
    )
    ax2.axhline(
        3.00, color="#555555", linestyle="-.", linewidth=1.0,
        label="Ambang Cut-Off Proteksi PCM LiPo (3,00 V)"
    )

    ax2.set_xlabel("Waktu Operasional (Jam)")
    ax2.set_ylabel("Tegangan Baterai LiPo 350 mAh (Volt)")
    ax2.set_xlim(0, 8.5)
    ax2.set_ylim(2.85, 4.35)
    ax2.grid(True, linestyle=":", color="#cccccc", alpha=0.8)
    ax2.legend(loc="upper right", frameon=True, facecolor="white", edgecolor="#cccccc", fontsize=7.8)

    plt.tight_layout()
    for d in FIG_DIRS:
        fig.savefig(os.path.join(d, "fig4_1_power_and_discharge_curve.png"), dpi=300, facecolor="white")
    plt.close(fig)
    print("Saved: fig4_1_power_and_discharge_curve.png")


if __name__ == "__main__":
    generate_bab4_power_data_and_plot()
