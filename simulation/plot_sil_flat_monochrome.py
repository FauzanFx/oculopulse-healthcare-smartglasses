#!/usr/bin/env python3
# OculoPulse Flat Monochrome Verification Plot Generator

import os
import csv
import numpy as np
import matplotlib.pyplot as plt

plt.style.use("default")

SIM_DIR  = os.path.dirname(os.path.abspath(__file__))
BASE_DIR = os.path.dirname(SIM_DIR)
CSV_PATH = os.path.join(SIM_DIR, "telemetry_log_4scenarios.csv")
LIN_CSV  = os.path.join(SIM_DIR, "firmware_linearity_test.csv")
LAT_CSV  = os.path.join(SIM_DIR, "firmware_latency_1000frames.csv")

FIG_DIRS = [os.path.join(BASE_DIR, "figures")]
if os.path.exists(os.path.join(BASE_DIR, "report")):
    FIG_DIRS.append(os.path.join(BASE_DIR, "report", "figures"))
for d in FIG_DIRS:
    os.makedirs(d, exist_ok=True)


def save_figure(fig, filename):
    for d in FIG_DIRS:
        out_p = os.path.join(d, filename)
        fig.savefig(out_p, dpi=300, facecolor="white")
    print("Saved:", filename)


def plot_fig3_1():
    d_true, d_2a, d_2b, err = [], [], [], []
    with open(LIN_CSV, "r", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            d_true.append(float(row["d_true_mm"]))
            d_2a.append(float(row["d_cpp_major_2a_mm"]))
            d_2b.append(float(row["d_uncomp_minor_2b_mm"]))
            err.append(float(row["abs_error_mm"]))

    f_idx, host_us, esp_ms = [], [], []
    with open(LAT_CSV, "r", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            f_idx.append(int(row["frame_idx"]))
            host_us.append(float(row["host_us"]))
            esp_ms.append(float(row["esp32s3_est_ms"]))

    d_true = np.array(d_true)
    d_2a   = np.array(d_2a)
    d_2b   = np.array(d_2b)
    f_idx  = np.array(f_idx)
    esp_ms = np.array(esp_ms)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10.5, 4.4), dpi=300)

    ax1.plot(d_true, d_true, color="#777777", linestyle="--", linewidth=1.1, label="Referensi Ideal Ground-Truth (y = x)")
    ax1.plot(d_true, d_2a, color="#111111", linestyle="-", marker="o", markersize=4.5, linewidth=1.4,
             label="Ekstraksi Sumbu Mayor 2a C++ (MAE = 0,021 mm)")
    ax1.plot(d_true, d_2b, color="#555555", linestyle=":", marker="s", markersize=4.0, linewidth=1.3,
             label="Tanpa Kompensasi Perspektif 25° (Galat -9,4%)")
    ax1.set_xlabel("Diameter Pupil Ground-Truth D_true (mm)")
    ax1.set_ylabel("Diameter Pupil Hasil Ekstraksi C++ (mm)")
    ax1.set_xlim(2.4, 6.6)
    ax1.set_ylim(2.1, 6.7)
    ax1.grid(True, linestyle=":", color="#cccccc", alpha=0.8)
    ax1.legend(loc="upper left", frameon=True, facecolor="white", edgecolor="#cccccc", fontsize=8.0)

    mean_ms = float(np.mean(esp_ms))
    ax2.plot(f_idx, esp_ms, color="#333333", linestyle="-", linewidth=0.8, label="Waktu Eksekusi per Frame (1.000 Siklus)")
    ax2.axhline(mean_ms, color="#111111", linestyle="--", linewidth=1.3,
                label=f"Rata-Rata Eksekusi ({mean_ms:.2f} ms/frame)")
    ax2.axhline(2.00, color="#666666", linestyle=":", linewidth=1.2,
                label="Batas Determinisme Real-Time (2,00 ms << 66,7 ms)")
    ax2.set_xlabel("Indeks Frame Uji Beruntun (Citra ROI 120×120 Piksel)")
    ax2.set_ylabel("Waktu Eksekusi Siklus Inti (ms)")
    ax2.set_xlim(1, 1000)
    ax2.set_ylim(0.5, 2.3)
    ax2.grid(True, linestyle=":", color="#cccccc", alpha=0.8)
    ax2.legend(loc="upper right", frameon=True, facecolor="white", edgecolor="#cccccc", fontsize=8.0)

    plt.tight_layout()
    save_figure(fig, "fig3_1_firmware_validation_proof.png")
    plt.close(fig)


def load_sil_csv():
    data = {
        "t": [], "lux": [], "d_ukur": [], "d_base": [],
        "delta_d": [], "cpui": [], "blink": [], "perclos": [], "pitch": []
    }
    with open(CSV_PATH, "r", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            data["t"].append(float(row["timestamp_s"]))
            data["lux"].append(float(row["ambient_lux"]))
            data["d_ukur"].append(float(row["pupil_measured_mm"]))
            data["d_base"].append(float(row["pupil_baseline_mm"]))
            data["delta_d"].append(float(row["delta_d_autonomic_mm"]))
            data["cpui"].append(float(row["cpui_mm_per_min"]))
            data["blink"].append(float(row["blink_rate_per_min"]))
            data["perclos"].append(float(row["perclos_pct"]))
            data["pitch"].append(float(row["head_pitch_deg"]))
    return {k: np.array(v) for k, v in data.items()}


def plot_fig3_2(d):
    t = d["t"]
    fig, axes = plt.subplots(5, 1, figsize=(10.5, 11.0), dpi=300, sharex=True)

    for ax in axes:
        for x_sep in [60, 120, 180]:
            ax.axvline(x_sep, color="#888888", linestyle="--", linewidth=0.9)
        ax.grid(True, linestyle=":", color="#cccccc", alpha=0.8)

    ax1 = axes[0]
    ax1.plot(t, d["d_ukur"], color="#111111", linestyle="-", linewidth=1.4, label="Diameter Pupil Terukur D_ukur (mm)")
    ax1.plot(t, d["d_base"], color="#555555", linestyle="--", linewidth=1.3, label="Referensi Cahaya D_baseline(E_v) (mm)")
    ax1.set_ylabel("Pupil (mm)")
    ax1.set_ylim(3.1, 5.2)
    ax1.legend(loc="upper left", frameon=True, facecolor="white", edgecolor="#cccccc", fontsize=8.5)

    ax1_r = ax1.twinx()
    ax1_r.plot(t, d["lux"], color="#777777", linestyle=":", linewidth=1.2, label="Iluminansi E_v (Lux)")
    ax1_r.set_ylabel("Cahaya (Lux)")
    ax1_r.set_ylim(200, 800)
    ax1_r.legend(loc="upper right", frameon=True, facecolor="white", edgecolor="#cccccc", fontsize=8.5)

    ax2 = axes[1]
    ax2.plot(t, d["delta_d"], color="#111111", linestyle="-", linewidth=1.4, label="Simpangan Otonom ΔD_otonom (mm)")
    ax2.axhline(0.40, color="#444444", linestyle="--", linewidth=1.0, label="Ambang Dilatasi Simpatis (+0,40 mm)")
    ax2.axhline(-0.35, color="#666666", linestyle=":", linewidth=1.1, label="Ambang Miosis Kantuk (-0,35 mm)")
    ax2.axhline(0.0, color="#999999", linestyle="-", linewidth=0.7)
    ax2.set_ylabel("ΔD_otonom (mm)")
    ax2.set_ylim(-0.80, 0.80)
    ax2.legend(loc="upper right", frameon=True, facecolor="white", edgecolor="#cccccc", fontsize=8.5)

    ax3 = axes[2]
    ax3.plot(t, d["cpui"], color="#111111", linestyle="-", linewidth=1.4, label="Indeks Ketidakstabilan Pupil cPUI (mm/min)")
    ax3.axhline(6.5, color="#555555", linestyle="--", linewidth=1.1, label="Ambang Gelombang Hippus Kantuk (6,5 mm/min)")
    ax3.set_ylabel("cPUI (mm/min)")
    ax3.set_ylim(0, 12.5)
    ax3.legend(loc="upper left", frameon=True, facecolor="white", edgecolor="#cccccc", fontsize=8.5)

    ax4 = axes[3]
    ax4.plot(t, d["blink"], color="#111111", linestyle="-", linewidth=1.4, label="Laju Kedip (/min)")
    ax4.plot(t, d["perclos"], color="#555555", linestyle="-.", linewidth=1.4, label="PERCLOS (%)")
    ax4.axhline(15.0, color="#444444", linestyle=":", linewidth=1.1, label="Ambang Microsleep PERCLOS (15%)")
    ax4.axhline(7.0, color="#777777", linestyle="--", linewidth=1.0, label="Ambang Mata Kering CVS (7 /min)")
    ax4.set_ylabel("Kedip & PERCLOS")
    ax4.set_ylim(0, 27)
    ax4.legend(loc="upper left", frameon=True, facecolor="white", edgecolor="#cccccc", fontsize=8.5, ncol=2)

    ax5 = axes[4]
    ax5.plot(t, d["pitch"], color="#111111", linestyle="-", linewidth=1.4, label="Sudut Pitch Leher θ_pitch (derajat)")
    ax5.axhline(20.0, color="#555555", linestyle="--", linewidth=1.1, label="Ambang Postur Text-Neck (20°)")
    ax5.set_ylabel("Pitch (derajat)")
    ax5.set_xlabel("Waktu Simulasi (detik)")
    ax5.set_ylim(0, 30)
    ax5.set_xlim(0, 240)
    ax5.legend(loc="upper left", frameon=True, facecolor="white", edgecolor="#cccccc", fontsize=8.5)

    plt.tight_layout()
    save_figure(fig, "fig3_2_sil_verification_timeseries.png")
    plt.close(fig)


def plot_fig3_3():
    ev = np.linspace(1, 1400, 500)
    ev_pow = np.power(ev, 0.41)
    d_base = 7.75 - 5.75 * (ev_pow / (ev_pow + 7.5))
    d_upper = d_base + 0.40
    d_lower = d_base - 0.35

    fig, ax = plt.subplots(figsize=(9.5, 5.0), dpi=300)

    ax.plot(ev, d_base, color="#111111", linestyle="-", linewidth=1.8, label="Kurva Referensi Cahaya D_baseline(E_v) [11]")
    ax.plot(ev, d_upper, color="#444444", linestyle="--", linewidth=1.2, label="Batas Atas Normal (+0,40 mm -> Ambang COGNITIVE_OVERLOAD)")
    ax.plot(ev, d_lower, color="#666666", linestyle=":", linewidth=1.3, label="Batas Bawah Normal (-0,35 mm -> Ambang FATIGUE_MICROSLEEP)")

    pts = [
        (300.0, 4.414, "o", "Titik 1: Normal @ 300 Lux (4,41 mm)", (330, 4.65)),
        (650.0, 3.984, "s", "Titik 2: Normal @ 650 Lux (3,98 mm)", (680, 4.10)),
        (650.0, 4.534, "^", "Titik 3: Cognitive Overload @ 650 Lux (4,53 mm)", (680, 4.80)),
        (480.0, 3.680, "v", "Titik 4: Fatigue Microsleep @ 480 Lux (3,68 mm)", (480, 3.20)),
    ]

    for x_pt, y_pt, marker, lbl, (tx, ty) in pts:
        ax.plot(x_pt, y_pt, marker=marker, color="#111111", markersize=7, linestyle="None", label=lbl)
        ax.annotate(
            lbl.split(":")[0],
            xy=(x_pt, y_pt),
            xytext=(tx, ty),
            arrowprops=dict(arrowstyle="->", color="#333333", lw=0.9),
            fontsize=8.5,
            color="#111111"
        )

    ax.set_xlabel("Iluminansi Cahaya Ruangan E_v (Lux)")
    ax.set_ylabel("Diameter Pupil (mm)")
    ax.set_xlim(0, 1400)
    ax.set_ylim(2.8, 6.8)
    ax.grid(True, linestyle=":", color="#cccccc", alpha=0.8)
    ax.legend(loc="upper right", frameon=True, facecolor="white", edgecolor="#cccccc", fontsize=8.0)

    plt.tight_layout()
    save_figure(fig, "fig3_3_light_compensation_proof.png")
    plt.close(fig)


def plot_fig2_7():
    t_ms = np.linspace(0, 200, 2000)
    shutter = np.zeros_like(t_ms)
    led_ir  = np.zeros_like(t_ms)
    dsp_ex  = np.zeros_like(t_ms)

    for start_t in [5.0, 71.7, 138.4]:
        shutter[(t_ms >= start_t) & (t_ms <= start_t + 8.0)] = 1.0
        led_ir[(t_ms >= start_t) & (t_ms <= start_t + 8.0)]  = 0.512
        dsp_ex[(t_ms >= start_t + 8.2) & (t_ms <= start_t + 9.4)] = 1.0

    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(9.5, 5.5), dpi=300, sharex=True)

    ax1.plot(t_ms, shutter, color="#111111", linewidth=1.4, label="Bukaan Rana Kamera OV2640 (15 FPS, T = 66,7 ms)")
    ax1.set_ylabel("Status Rana")
    ax1.set_yticks([0, 1])
    ax1.set_yticklabels(["IDLE", "OPEN"])
    ax1.set_ylim(-0.2, 1.3)
    ax1.grid(True, linestyle=":", color="#cccccc")
    ax1.legend(loc="upper right", frameon=True, facecolor="white", edgecolor="#cccccc", fontsize=8.5)

    ax2.plot(t_ms, led_ir, color="#111111", linewidth=1.4, label="Pulsa Strobe LED NIR 850 nm (8,0 ms, Duty Cycle 12%)")
    ax2.axhline(0.0614, color="#555555", linestyle="--", linewidth=1.1, label="Iradians Rata-Rata (0,0614 mW/cm² << 10 mW/cm²)")
    ax2.set_ylabel("Iradians\n(mW/cm²)")
    ax2.set_ylim(-0.05, 0.65)
    ax2.grid(True, linestyle=":", color="#cccccc")
    ax2.legend(loc="upper right", frameon=True, facecolor="white", edgecolor="#cccccc", fontsize=8.5)

    ax3.plot(t_ms, dsp_ex, color="#111111", linewidth=1.4, label="Eksekusi Visi & Fusi C++ di SRAM (~1,16 ms)")
    ax3.set_ylabel("Status CPU")
    ax3.set_xlabel("Waktu Operasional (milidetik / ms)")
    ax3.set_yticks([0, 1])
    ax3.set_yticklabels(["SLEEP", "COMPUTE"])
    ax3.set_ylim(-0.2, 1.3)
    ax3.set_xlim(0, 200)
    ax3.grid(True, linestyle=":", color="#cccccc")
    ax3.legend(loc="upper right", frameon=True, facecolor="white", edgecolor="#cccccc", fontsize=8.5)

    plt.tight_layout()
    save_figure(fig, "fig2_7_pwm_timing_diagram.png")
    plt.close(fig)


if __name__ == "__main__":
    plot_fig3_1()
    data = load_sil_csv()
    plot_fig3_2(data)
    plot_fig3_3()
    plot_fig2_7()
