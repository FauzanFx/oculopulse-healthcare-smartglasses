#!/usr/bin/env python3
# OculoPulse Python Reference Multi-Sensor Fusion Simulator (240s, 4 Scenarios)

import os
import json
import csv
import numpy as np

SIM_DIR = os.path.dirname(os.path.abspath(__file__))


def calc_d_baseline(lux: float) -> float:
    ev = max(0.0, float(lux))
    ev_pow = ev ** 0.41
    return 7.75 - 5.75 * (ev_pow / (ev_pow + 7.5))


def run_sil_simulation():
    np.random.seed(42)
    duration_sec = 240
    telemetry_1hz = []
    delta_d_hist = []

    for t in range(duration_sec):
        if t < 60:
            phase = "NORMAL_ALERT"
            lux = 300.0 + (350.0 / (1.0 + np.exp(-(t - 30.0) / 2.5))) + np.random.normal(0, 3.0)
            true_delta_d = 0.04 * np.sin(2 * np.pi * 0.05 * t) + np.random.normal(0, 0.025)
            blink_rate = 16.0 + np.random.normal(0, 0.8)
            blink_dur_ms = 165.0 + np.random.normal(0, 8.0)
            perclos = 3.2 + np.random.normal(0, 0.4)
            ear_mean = 0.292 + np.random.normal(0, 0.006)
            pitch_deg = 6.5 + 1.2 * np.sin(2 * np.pi * 0.03 * t) + np.random.normal(0, 0.4)
            gyro_y_dps = np.random.normal(0, 3.0)
            a_dyn_g = 0.03 + abs(np.random.normal(0, 0.015))
            if 44 <= t <= 47:
                a_dyn_g = 0.28 + np.random.uniform(0.0, 0.06)

        elif t < 120:
            phase = "COGNITIVE_OVERLOAD"
            lux = 650.0 + np.random.normal(0, 4.0)
            ramp = min(1.0, (t - 60) / 8.0)
            true_delta_d = 0.56 * ramp + 0.05 * np.sin(2 * np.pi * 0.08 * t) + np.random.normal(0, 0.03)
            blink_rate = 10.5 + np.random.normal(0, 0.7)
            blink_dur_ms = 155.0 + np.random.normal(0, 7.0)
            perclos = 2.6 + np.random.normal(0, 0.3)
            ear_mean = 0.305 + np.random.normal(0, 0.005)
            pitch_deg = 9.0 + np.random.normal(0, 0.6)
            gyro_y_dps = np.random.normal(0, 3.5)
            a_dyn_g = 0.04 + abs(np.random.normal(0, 0.015))

        elif t < 180:
            phase = "CVS_DRY_EYE_STRAIN"
            lux = 500.0 + np.random.normal(0, 4.0)
            true_delta_d = 0.10 + 0.04 * np.sin(2 * np.pi * 0.04 * t) + np.random.normal(0, 0.025)
            ramp = min(1.0, (t - 120) / 10.0)
            blink_rate = 11.0 - 7.0 * ramp + np.random.normal(0, 0.4)
            blink_dur_ms = 180.0 + np.random.normal(0, 9.0)
            perclos = 1.8 + np.random.normal(0, 0.3)
            ear_mean = 0.308 + np.random.normal(0, 0.005)
            pitch_deg = 10.0 + 14.5 * ramp + np.random.normal(0, 0.6)
            gyro_y_dps = np.random.normal(0, 2.5)
            a_dyn_g = 0.025 + abs(np.random.normal(0, 0.01))

        else:
            phase = "FATIGUE_MICROSLEEP"
            lux = 480.0 + np.random.normal(0, 3.5)
            ramp = min(1.0, (t - 180) / 10.0)
            hippus_wave = 0.22 * ramp * np.sin(2 * np.pi * 0.35 * t)
            true_delta_d = -0.46 * ramp + hippus_wave + np.random.normal(0, 0.03)
            blink_rate = 14.0 - 6.5 * ramp + np.random.normal(0, 0.8)
            blink_dur_ms = 200.0 + 240.0 * ramp + np.random.normal(0, 12.0)
            perclos = 4.0 + 18.5 * ramp + np.random.normal(0, 0.9)
            ear_mean = 0.280 - 0.145 * ramp + np.random.normal(0, 0.012)
            if t in [205, 206, 222, 223, 234, 235]:
                ear_mean = 0.075 + np.random.uniform(-0.01, 0.01)
                pitch_deg = 23.5 + np.random.uniform(0, 3.0)
                gyro_y_dps = 44.0 + np.random.uniform(0, 8.0)
            else:
                pitch_deg = 16.5 + 3.0 * np.sin(2 * np.pi * 0.08 * t) + np.random.normal(0, 0.8)
                gyro_y_dps = 8.0 * np.cos(2 * np.pi * 0.08 * t) + np.random.normal(0, 4.0)
            a_dyn_g = 0.04 + abs(np.random.normal(0, 0.02))

        d_base = calc_d_baseline(lux)
        d_ukur = d_base + true_delta_d
        delta_d_otonom = d_ukur - d_base

        motion_gate = bool(a_dyn_g > 0.20)
        if not motion_gate or len(delta_d_hist) == 0:
            delta_d_hist.append(delta_d_otonom)
        else:
            delta_d_hist.append(delta_d_hist[-1])

        window = delta_d_hist[-60:]
        if len(window) >= 2:
            diffs = [abs(window[i] - window[i - 1]) for i in range(1, len(window))]
            cpui = (60.0 / len(diffs)) * sum(diffs)
        else:
            cpui = 2.0

        if perclos >= 15.0 or (blink_dur_ms > 350.0 and cpui >= 6.5) or (delta_d_otonom < -0.35 and cpui >= 6.5):
            pred_state = "FATIGUE_MICROSLEEP"
        elif blink_rate < 7.0 or pitch_deg > 20.0:
            pred_state = "CVS_DRY_EYE_STRAIN"
        elif delta_d_otonom > 0.40:
            pred_state = "COGNITIVE_OVERLOAD"
        else:
            pred_state = "NORMAL_ALERT"

        telemetry_1hz.append({
            "timestamp_s": t,
            "ground_truth_phase": phase,
            "ambient_lux": round(float(lux), 2),
            "pupil_measured_mm": round(float(d_ukur), 3),
            "pupil_baseline_mm": round(float(d_base), 3),
            "delta_d_autonomic_mm": round(float(delta_d_otonom), 3),
            "cpui_mm_per_min": round(float(cpui), 2),
            "ear_current": round(float(ear_mean), 3),
            "blink_rate_per_min": round(float(blink_rate), 2),
            "perclos_pct": round(float(perclos), 2),
            "head_pitch_deg": round(float(pitch_deg), 2),
            "accel_dyn_g": round(float(a_dyn_g), 3),
            "motion_gate_freeze": int(motion_gate),
            "head_nod_detected": int(gyro_y_dps > 35.0 and ear_mean < 0.165),
            "classified_state": pred_state,
            "haptic_lra_pwm_hz": 200 if pred_state == "FATIGUE_MICROSLEEP" else (170 if pred_state == "CVS_DRY_EYE_STRAIN" else 0),
        })

    out_json = os.path.join(SIM_DIR, "python_ref_telemetry_4scenarios.json")
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump(telemetry_1hz, f, indent=2)
    print("Saved Python reference telemetry:", out_json)
    return telemetry_1hz


if __name__ == "__main__":
    run_sil_simulation()
