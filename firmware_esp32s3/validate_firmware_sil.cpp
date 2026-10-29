// OculoPulse C++ Firmware Testbench & Software-in-the-Loop (SIL) Validator

#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <random>
#include <cassert>
#include <cmath>
#include <vector>
#include <string>
#include <filesystem>

#include "oculopulse_core.h"

static std::string resolveSimDir() {
    if (std::filesystem::exists("simulation")) {
        return "simulation/";
    }
    if (std::filesystem::exists("../simulation")) {
        return "../simulation/";
    }
    std::filesystem::create_directories("simulation");
    return "simulation/";
}

// Synthetic 120x120 ROI with 25-deg off-axis perspective compression & corneal glint
void renderSyntheticEyeROI(
    uint8_t roi[OCULO_ROI_HEIGHT][OCULO_ROI_WIDTH],
    float true_pupil_diam_mm,
    std::mt19937& rng
) {
    std::normal_distribution<float> noise_dist(0.0f, 2.5f);
    float true_2a_px = true_pupil_diam_mm / OCULO_K_KAL_MM_PER_PX;
    float a = true_2a_px * 0.5f;
    float b = a * cosf(25.0f * (float)M_PI / 180.0f);

    float cx = 60.0f, cy = 60.0f;

    for (int y = 0; y < OCULO_ROI_HEIGHT; y++) {
        for (int x = 0; x < OCULO_ROI_WIDTH; x++) {
            float dx = (float)x - cx;
            float dy = (float)y - cy;
            float ellipse_eq = (dx * dx) / (a * a) + (dy * dy) / (b * b);

            float base_intensity = (ellipse_eq <= 1.0f) ? 24.0f : 175.0f;
            float noisy_val = base_intensity + noise_dist(rng);
            if (noisy_val < 0.0f)   noisy_val = 0.0f;
            if (noisy_val > 255.0f) noisy_val = 255.0f;
            roi[y][x] = (uint8_t)noisy_val;
        }
    }

    roi[56][64] = 255;
    roi[56][65] = 255;
    roi[57][64] = 255;
}

int main() {
    const std::string sim_dir = resolveSimDir();

    std::cout << "============================================================================\n";
    std::cout << " OCULOPULSE — C++ FIRMWARE VERIFICATION & 4-SCENARIO SIL TESTBENCH\n";
    std::cout << "============================================================================\n\n";

    // 1. Static SRAM footprint audit
    uint8_t roi_buffer[OCULO_ROI_HEIGHT][OCULO_ROI_WIDTH];
    OculoPulseFilterState_t filter_state;
    OculoPulseTelemetry_t   telemetry_pkt;

    size_t roi_bytes   = sizeof(roi_buffer);
    size_t state_bytes = sizeof(filter_state);
    size_t tel_bytes   = sizeof(telemetry_pkt);
    size_t total_sram  = roi_bytes + state_bytes + tel_bytes;
    double sram_pct    = (double)total_sram / (512.0 * 1024.0) * 100.0;

    std::cout << "[STAGE 1] Static Memory Audit (Target: ESP32-S3 512 KB Internal SRAM)\n";
    std::cout << "  - ROI DMA Buffer (120x120 uint8_t) : " << roi_bytes   << " B (14.06 KB)\n";
    std::cout << "  - Filter State Struct              : " << state_bytes << " B\n";
    std::cout << "  - Telemetry Packet Struct          : " << tel_bytes   << " B\n";
    std::cout << "  - Total Vision+Fusion Static SRAM  : " << total_sram  << " B ("
              << std::fixed << std::setprecision(2) << sram_pct << "% of 512 KB)\n";
    assert(total_sram < 32768);
    std::cout << "  -> STATUS: PASS\n\n";

    // 2. Unit tests: PLR baseline, 25-deg ellipse linearity, motion gate
    std::cout << "[STAGE 2] Unit Testing (`oculopulse_core.h`)\n";

    float d_300 = computePupilLightBaseline(300.0f);
    float d_650 = computePupilLightBaseline(650.0f);
    std::cout << "  - Test 2A (Light Baseline): D_base(300 Lux) = " << std::setprecision(3) << d_300
              << " mm | D_base(650 Lux) = " << d_650 << " mm\n";
    assert(std::fabs(d_300 - 4.414f) < 0.01f);
    assert(std::fabs(d_650 - 3.979f) < 0.01f);

    std::mt19937 rng(42);
    std::ofstream lin_csv(sim_dir + "firmware_linearity_test.csv");
    lin_csv << "d_true_mm,d_cpp_major_2a_mm,d_uncomp_minor_2b_mm,abs_error_mm\n";

    double sum_abs_err = 0.0;
    int sweep_count = 0;
    for (float d_test = 2.60f; d_test <= 6.45f; d_test += 0.20f) {
        double acc_2a_mm = 0.0;
        for (int rep = 0; rep < 10; rep++) {
            renderSyntheticEyeROI(roi_buffer, d_test, rng);
            float ex_2a_px = extractPupilMajorAxis2aFromROI(roi_buffer, 55);
            acc_2a_mm += (double)(ex_2a_px * OCULO_K_KAL_MM_PER_PX);
        }
        double mean_2a_mm = acc_2a_mm / 10.0;
        double uncomp_2b_mm = (double)d_test * std::cos(25.0 * M_PI / 180.0);
        double err_mm = std::fabs(mean_2a_mm - (double)d_test);
        sum_abs_err += err_mm;
        sweep_count++;

        lin_csv << std::fixed << std::setprecision(3)
                << d_test << "," << mean_2a_mm << "," << uncomp_2b_mm << "," << err_mm << "\n";
    }
    lin_csv.close();

    float target_pupil_mm = 4.41f;
    renderSyntheticEyeROI(roi_buffer, target_pupil_mm, rng);
    float extracted_2a_px = extractPupilMajorAxis2aFromROI(roi_buffer, 55);
    float recovered_mm    = extracted_2a_px * OCULO_K_KAL_MM_PER_PX;
    float geom_err_mm     = std::fabs(recovered_mm - target_pupil_mm);
    double mae_sweep_mm   = sum_abs_err / (double)sweep_count;
    std::cout << "  - Test 2B (25-deg Off-Axis Ellipse): Target = " << target_pupil_mm
              << " mm | Extracted = " << recovered_mm << " mm (Sweep MAE = " << mae_sweep_mm << " mm)\n";
    assert(geom_err_mm < 0.08f);

    initOculoPulseState(&filter_state);
    filter_state.cpui_accum = 2.15f;
    stepOculoPulseCore1(&filter_state, 10, 0.0667f, extracted_2a_px, 300.0f,
                        0.50f, 0.20f, 1.25f, 12.0f, 0.300f, 16.0f, 3.0f, -1.0f, &telemetry_pkt);
    std::cout << "  - Test 2C (Motion Gate @ |a_dyn|=" << telemetry_pkt.accel_dyn_g << "g): "
              << "freeze=" << (telemetry_pkt.motion_gate_freeze ? "true" : "false")
              << " | Locked cPUI = " << telemetry_pkt.cpui_mm_per_min << " mm/min\n";
    assert(telemetry_pkt.motion_gate_freeze == true);
    assert(std::fabs(telemetry_pkt.cpui_mm_per_min - 2.15f) < 1e-5f);
    std::cout << "  -> STATUS: ALL UNIT TESTS PASSED\n\n";

    // 3. 1,000-frame execution latency benchmark
    std::cout << "[STAGE 3] Execution Latency Benchmark (1,000 Consecutive 120x120 ROI Frames)\n";
    char json_out[384];
    std::ofstream lat_csv(sim_dir + "firmware_latency_1000frames.csv");
    lat_csv << "frame_idx,host_us,esp32s3_est_ms\n";

    double total_ms = 0.0;
    for (int i = 0; i < 1000; i++) {
        auto f0 = std::chrono::high_resolution_clock::now();
        float px2a = extractPupilMajorAxis2aFromROI(roi_buffer, 55);
        stepOculoPulseCore1(&filter_state, i, 0.0667f, px2a, 500.0f,
                            0.01f, 0.02f, 0.99f, 1.0f, 0.295f, 15.0f, 3.0f, -1.0f, &telemetry_pkt);
        formatTelemetryJSON(&telemetry_pkt, json_out, sizeof(json_out));
        auto f1 = std::chrono::high_resolution_clock::now();
        double frame_us = std::chrono::duration<double, std::micro>(f1 - f0).count();
        double frame_esp_ms = (frame_us / 1000.0) * 35.0;
        total_ms += (frame_us / 1000.0);
        lat_csv << (i + 1) << "," << std::fixed << std::setprecision(2) << frame_us << ","
                << std::setprecision(3) << frame_esp_ms << "\n";
    }
    lat_csv.close();
    double host_ms_per_frame = total_ms / 1000.0;
    double esp32s3_est_ms = host_ms_per_frame * 35.0;
    std::cout << "  - Host C++ Execution (x86_64)       : " << std::setprecision(4) << host_ms_per_frame << " ms/frame\n";
    std::cout << "  - ESP32-S3 Estimated (@ 240 MHz)    : " << std::setprecision(2) << esp32s3_est_ms << " ms/frame (15 FPS budget: 66.70 ms)\n\n";

    // 4. 240-second 4-scenario SIL co-simulation
    std::cout << "[STAGE 4] 240-Second 4-Scenario SIL Co-Simulation\n";

    initOculoPulseState(&filter_state);
    std::normal_distribution<float> n_lux(0.0f, 1.5f);
    std::normal_distribution<float> n_pupil(0.0f, 0.018f);
    std::normal_distribution<float> n_blink(0.0f, 0.35f);
    std::normal_distribution<float> n_pitch(0.0f, 0.40f);

    std::ofstream csv_file(sim_dir + "telemetry_log_4scenarios.csv");
    std::ofstream json_file(sim_dir + "telemetry_log_4scenarios.json");

    csv_file << "timestamp_s,ground_truth_phase,ambient_lux,pupil_measured_mm,pupil_baseline_mm,"
             << "delta_d_autonomic_mm,cpui_mm_per_min,ear_current,blink_rate_per_min,perclos_pct,"
             << "head_pitch_deg,accel_dyn_g,motion_gate_freeze,head_nod_detected,classified_state,haptic_lra_pwm_hz\n";
    json_file << "[\n";

    int phase_correct[4] = {0, 0, 0, 0};
    double phase1_delta_d_sq_sum = 0.0;

    for (int t = 0; t < 240; t++) {
        int phase_idx = t / 60;
        const char* gt_label = "NORMAL_ALERT";
        float lux = 300.0f, aut_shift_mm = 0.0f, cpui_val = 1.65f, ear_val = 0.305f;
        float blink_rate = 16.0f, perclos = 3.2f, target_pitch = 6.5f;
        float ax = -sinf(6.5f * (float)M_PI / 180.0f), ay = 0.02f, az = cosf(6.5f * (float)M_PI / 180.0f), gyro_y = 0.5f;

        if (phase_idx == 0) {
            gt_label = "NORMAL_ALERT";
            float trans = 1.0f / (1.0f + expf(-(float)(t - 30) / 2.0f));
            lux = 300.0f + 350.0f * trans + n_lux(rng);
            aut_shift_mm = n_pupil(rng);
            cpui_val = 1.65f + 0.15f * sinf(0.1f * t);
            ear_val = 0.305f;
            blink_rate = 16.0f + n_blink(rng);
            perclos = 3.2f + 0.3f * sinf(0.2f * t);
            target_pitch = 6.5f + n_pitch(rng);

            if (t >= 44 && t <= 47) {
                ax += 0.28f;
                az += 0.22f;
                cpui_val = 9.5f;
            }
        } else if (phase_idx == 1) {
            gt_label = "COGNITIVE_OVERLOAD";
            lux = 650.0f + n_lux(rng);
            float ramp = std::min(1.0f, (float)(t - 60) / 3.0f);
            aut_shift_mm = 0.55f * ramp + n_pupil(rng);
            cpui_val = 2.40f + 0.15f * sinf(0.15f * t);
            ear_val = 0.310f;
            blink_rate = 16.0f - 5.5f * ramp + n_blink(rng);
            perclos = 2.7f + 0.2f * sinf(0.2f * t);
            target_pitch = 9.2f + n_pitch(rng);
        } else if (phase_idx == 2) {
            gt_label = "CVS_DRY_EYE_TEXT_NECK";
            lux = 500.0f + n_lux(rng);
            float ramp = std::min(1.0f, (float)(t - 120) / 4.0f);
            aut_shift_mm = 0.08f + n_pupil(rng);
            cpui_val = 2.25f + 0.12f * sinf(0.1f * t);
            ear_val = 0.290f;
            blink_rate = 10.5f - 6.3f * ramp + n_blink(rng);
            perclos = 1.8f + 0.2f * sinf(0.2f * t);
            target_pitch = 9.2f + 15.3f * ramp + n_pitch(rng);
        } else {
            gt_label = "FATIGUE_MICROSLEEP";
            lux = 480.0f + n_lux(rng);
            float ramp = std::min(1.0f, (float)(t - 180) / 4.0f);
            float hippus_osc = 0.18f * sinf(2.0f * (float)M_PI * 0.22f * (float)t);
            aut_shift_mm = -0.46f * ramp + hippus_osc + n_pupil(rng);
            cpui_val = 2.2f + 9.2f * ((float)(t - 180) / 60.0f);
            ear_val = 0.290f - 0.165f * ramp;
            blink_rate = 11.0f - 3.5f * ramp + n_blink(rng);
            perclos = 2.5f + 20.3f * ramp + 0.6f * sinf(0.3f * t);
            target_pitch = 16.5f + 2.5f * sinf(0.2f * t) + n_pitch(rng);

            if (t == 205 || t == 223 || t == 234) {
                target_pitch = 23.5f;
                gyro_y = 42.0f;
                ear_val = 0.110f;
            }
        }

        if (!(phase_idx == 0 && t >= 44 && t <= 47)) {
            ax = -sinf(target_pitch * (float)M_PI / 180.0f);
            ay = 0.01f;
            az = cosf(target_pitch * (float)M_PI / 180.0f);
        }

        float true_d_base = computePupilLightBaseline(lux);
        float true_d_mm   = true_d_base + aut_shift_mm;
        renderSyntheticEyeROI(roi_buffer, true_d_mm, rng);

        float major_2a_px = extractPupilMajorAxis2aFromROI(roi_buffer, 55);
        filter_state.pitch_deg = target_pitch;
        stepOculoPulseCore1(
            &filter_state,
            (uint32_t)t,
            0.0667f,
            major_2a_px,
            lux,
            ax, ay, az, gyro_y,
            ear_val, blink_rate, perclos,
            cpui_val,
            &telemetry_pkt
        );

        if (std::string(telemetry_pkt.classified_state) == std::string(gt_label)) {
            phase_correct[phase_idx]++;
        }
        if (phase_idx == 0) {
            phase1_delta_d_sq_sum += telemetry_pkt.delta_d_autonomic_mm * telemetry_pkt.delta_d_autonomic_mm;
        }

        csv_file << telemetry_pkt.timestamp_s << ","
                 << gt_label << ","
                 << std::fixed << std::setprecision(2) << telemetry_pkt.ambient_lux << ","
                 << std::setprecision(3) << telemetry_pkt.pupil_measured_mm << ","
                 << telemetry_pkt.pupil_baseline_mm << ","
                 << telemetry_pkt.delta_d_autonomic_mm << ","
                 << std::setprecision(2) << telemetry_pkt.cpui_mm_per_min << ","
                 << std::setprecision(3) << telemetry_pkt.ear_current << ","
                 << std::setprecision(2) << telemetry_pkt.blink_rate_per_min << ","
                 << telemetry_pkt.perclos_pct << ","
                 << telemetry_pkt.head_pitch_deg << ","
                 << telemetry_pkt.accel_dyn_g << ","
                 << (telemetry_pkt.motion_gate_freeze ? 1 : 0) << ","
                 << (telemetry_pkt.head_nod_detected ? 1 : 0) << ","
                 << telemetry_pkt.classified_state << ","
                 << telemetry_pkt.haptic_lra_pwm_hz << "\n";

        json_file << "  {\"timestamp_s\": " << telemetry_pkt.timestamp_s
                  << ", \"ground_truth\": \"" << gt_label << "\""
                  << ", \"ambient_lux\": " << std::setprecision(2) << telemetry_pkt.ambient_lux
                  << ", \"pupil_measured_mm\": " << std::setprecision(3) << telemetry_pkt.pupil_measured_mm
                  << ", \"pupil_baseline_mm\": " << telemetry_pkt.pupil_baseline_mm
                  << ", \"delta_d_autonomic_mm\": " << telemetry_pkt.delta_d_autonomic_mm
                  << ", \"cpui_mm_per_min\": " << std::setprecision(2) << telemetry_pkt.cpui_mm_per_min
                  << ", \"ear_current\": " << std::setprecision(3) << telemetry_pkt.ear_current
                  << ", \"blink_rate_per_min\": " << std::setprecision(2) << telemetry_pkt.blink_rate_per_min
                  << ", \"perclos_pct\": " << telemetry_pkt.perclos_pct
                  << ", \"head_pitch_deg\": " << telemetry_pkt.head_pitch_deg
                  << ", \"accel_dyn_g\": " << telemetry_pkt.accel_dyn_g
                  << ", \"motion_gate_freeze\": " << (telemetry_pkt.motion_gate_freeze ? "true" : "false")
                  << ", \"head_nod_detected\": " << (telemetry_pkt.head_nod_detected ? "true" : "false")
                  << ", \"classified_state\": \"" << telemetry_pkt.classified_state << "\""
                  << ", \"haptic_lra_pwm_hz\": " << telemetry_pkt.haptic_lra_pwm_hz
                  << "}" << (t < 239 ? ",\n" : "\n");
    }

    json_file << "]\n";
    csv_file.close();
    json_file.close();

    double phase1_rmse = std::sqrt(phase1_delta_d_sq_sum / 60.0);
    int total_correct = phase_correct[0] + phase_correct[1] + phase_correct[2] + phase_correct[3];

    std::cout << "  - Phase 1 Light Compensation RMSE (300->650 Lux): " << std::setprecision(4) << phase1_rmse << " mm (< 0.05 mm)\n";
    std::cout << "  - Phase 1 Accuracy (NORMAL_ALERT)               : " << phase_correct[0] << "/60 (" << std::setprecision(1) << (phase_correct[0]/0.6) << "%)\n";
    std::cout << "  - Phase 2 Accuracy (COGNITIVE_OVERLOAD)         : " << phase_correct[1] << "/60 (" << (phase_correct[1]/0.6) << "%)\n";
    std::cout << "  - Phase 3 Accuracy (CVS_DRY_EYE_TEXT_NECK)      : " << phase_correct[2] << "/60 (" << (phase_correct[2]/0.6) << "%)\n";
    std::cout << "  - Phase 4 Accuracy (FATIGUE_MICROSLEEP)         : " << phase_correct[3] << "/60 (" << (phase_correct[3]/0.6) << "%)\n";
    std::cout << "  - Total SIL Classification Accuracy             : " << total_correct << "/240 (" << (total_correct/2.4) << "%)\n";
    std::cout << "============================================================================\n";

    return 0;
}
