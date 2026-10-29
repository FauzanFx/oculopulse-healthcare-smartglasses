#ifndef OCULOPULSE_CORE_H
#define OCULOPULSE_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define OCULO_ROI_WIDTH            120
#define OCULO_ROI_HEIGHT           120
#define OCULO_K_KAL_MM_PER_PX      0.1378125f // 4.41 mm / 32.0 px @ 25.0 mm working distance
#define OCULO_EAR_BASELINE_DEF     0.300f
#define OCULO_ALPHA_COMP_FILTER    0.96f
#define OCULO_MOTION_GATE_THRESH_G 0.20f

typedef struct {
    uint32_t timestamp_s;
    float    ambient_lux;
    float    pupil_measured_mm;
    float    pupil_baseline_mm;
    float    delta_d_autonomic_mm;
    float    cpui_mm_per_min;
    float    ear_current;
    float    blink_rate_per_min;
    float    perclos_pct;
    float    head_pitch_deg;
    float    accel_dyn_g;
    bool     motion_gate_freeze;
    bool     head_nod_detected;
    char     classified_state[24];
    uint16_t haptic_lra_pwm_hz;
} OculoPulseTelemetry_t;

typedef struct {
    float pitch_deg;
    float prev_delta_d_mm;
    float cpui_accum;
    bool  initialized;
} OculoPulseFilterState_t;

static inline void initOculoPulseState(OculoPulseFilterState_t* st) {
    st->pitch_deg       = 6.5f;
    st->prev_delta_d_mm = 0.0f;
    st->cpui_accum      = 1.65f;
    st->initialized     = true;
}

// Watson & Yellott (2012) pupil light reflex baseline (mm)
static inline float computePupilLightBaseline(float ambient_lux) {
    float ev = (ambient_lux < 0.0f) ? 0.0f : ambient_lux;
    float ev_pow = powf(ev, 0.41f);
    return 7.75f - 5.75f * (ev_pow / (ev_pow + 7.5f));
}

// 2x2 covariance max eigenvalue for perspective-invariant major axis 2a (px)
static inline float extractPupilMajorAxis2aFromROI(
    const uint8_t roi[OCULO_ROI_HEIGHT][OCULO_ROI_WIDTH],
    uint8_t dark_thresh
) {
    uint32_t count = 0;
    double sum_x = 0.0, sum_y = 0.0;

    for (int y = 0; y < OCULO_ROI_HEIGHT; y++) {
        for (int x = 0; x < OCULO_ROI_WIDTH; x++) {
            if (roi[y][x] <= dark_thresh) {
                sum_x += (double)x;
                sum_y += (double)y;
                count++;
            }
        }
    }

    if (count < 16) {
        return 28.0f;
    }

    double cx = sum_x / (double)count;
    double cy = sum_y / (double)count;

    double cov_xx = 0.0, cov_yy = 0.0, cov_xy = 0.0;
    for (int y = 0; y < OCULO_ROI_HEIGHT; y++) {
        for (int x = 0; x < OCULO_ROI_WIDTH; x++) {
            if (roi[y][x] <= dark_thresh) {
                double dx = (double)x - cx;
                double dy = (double)y - cy;
                cov_xx += dx * dx;
                cov_yy += dy * dy;
                cov_xy += dx * dy;
            }
        }
    }

    cov_xx /= (double)count;
    cov_yy /= (double)count;
    cov_xy /= (double)count;

    double trace = cov_xx + cov_yy;
    double diff  = cov_xx - cov_yy;
    double disc  = sqrt(diff * diff + 4.0 * cov_xy * cov_xy);
    double lambda_max = 0.5 * (trace + disc);
    if (lambda_max < 1.0) lambda_max = 1.0;

    float major_axis_2a = (float)(4.0 * sqrt(lambda_max));
    if (major_axis_2a < 14.0f) major_axis_2a = 14.0f;
    if (major_axis_2a > 52.0f) major_axis_2a = 52.0f;
    return major_axis_2a;
}

static inline void stepOculoPulseCore1(
    OculoPulseFilterState_t* st,
    uint32_t timestamp_s,
    float    dt_sec,
    float    major_axis_2a_px,
    float    ambient_lux,
    float    ax_g,
    float    ay_g,
    float    az_g,
    float    gyro_y_dps,
    float    ear_current,
    float    blink_rate_per_min,
    float    perclos_pct,
    float    cpui_inst_override,
    OculoPulseTelemetry_t* out_tel
) {
    if (!st->initialized) {
        initOculoPulseState(st);
    }

    // IMU complementary filter & motion artifact gating
    float accel_pitch = atan2f(-ax_g, sqrtf(ay_g * ay_g + az_g * az_g)) * (180.0f / (float)M_PI);
    st->pitch_deg = OCULO_ALPHA_COMP_FILTER * (st->pitch_deg + gyro_y_dps * dt_sec)
                  + (1.0f - OCULO_ALPHA_COMP_FILTER) * accel_pitch;

    float accel_norm_g  = sqrtf(ax_g * ax_g + ay_g * ay_g + az_g * az_g);
    float a_dyn_g       = fabsf(accel_norm_g - 1.0f);
    bool  motion_freeze = (a_dyn_g > OCULO_MOTION_GATE_THRESH_G);

    // Light-compensated autonomic pupil deviation
    float d_ukur_mm  = OCULO_K_KAL_MM_PER_PX * major_axis_2a_px;
    float d_base_mm  = computePupilLightBaseline(ambient_lux);
    float delta_d_mm = d_ukur_mm - d_base_mm;

    if (!motion_freeze) {
        if (cpui_inst_override >= 0.0f) {
            st->cpui_accum = cpui_inst_override;
        } else {
            st->cpui_accum = 0.92f * st->cpui_accum + 14.5f * fabsf(delta_d_mm - st->prev_delta_d_mm);
        }
        st->prev_delta_d_mm = delta_d_mm;
    }

    // 4-state hierarchical decision tree
    bool head_nod = (gyro_y_dps > 35.0f) && (ear_current < (0.55f * OCULO_EAR_BASELINE_DEF));

    const char* state_str = "NORMAL_ALERT";
    uint16_t    lra_hz    = 0;

    if (perclos_pct >= 15.0f || st->cpui_accum >= 6.5f || head_nod) {
        state_str = "FATIGUE_MICROSLEEP";
        lra_hz    = 200;
    } else if (delta_d_mm > 0.40f && blink_rate_per_min < 12.5f) {
        state_str = "COGNITIVE_OVERLOAD";
        lra_hz    = 0;
    } else if (blink_rate_per_min < 7.0f || st->pitch_deg > 20.0f) {
        state_str = "CVS_DRY_EYE_STRAIN";
        lra_hz    = 170;
    }

    out_tel->timestamp_s          = timestamp_s;
    out_tel->ambient_lux          = ambient_lux;
    out_tel->pupil_measured_mm    = d_ukur_mm;
    out_tel->pupil_baseline_mm    = d_base_mm;
    out_tel->delta_d_autonomic_mm = delta_d_mm;
    out_tel->cpui_mm_per_min      = st->cpui_accum;
    out_tel->ear_current          = ear_current;
    out_tel->blink_rate_per_min   = blink_rate_per_min;
    out_tel->perclos_pct          = perclos_pct;
    out_tel->head_pitch_deg       = st->pitch_deg;
    out_tel->accel_dyn_g          = a_dyn_g;
    out_tel->motion_gate_freeze   = motion_freeze;
    out_tel->head_nod_detected    = head_nod;
    strncpy(out_tel->classified_state, state_str, sizeof(out_tel->classified_state) - 1);
    out_tel->classified_state[sizeof(out_tel->classified_state) - 1] = '\0';
    out_tel->haptic_lra_pwm_hz    = lra_hz;
}

static inline int formatTelemetryJSON(const OculoPulseTelemetry_t* snap, char* buf, size_t buf_size) {
    return snprintf(
        buf, buf_size,
        "{\"ts\":%lu,\"lux\":%.1f,\"d_ukur\":%.3f,\"d_base\":%.3f,"
        "\"delta_d\":%.3f,\"cpui\":%.2f,\"ear\":%.3f,\"blink\":%.1f,"
        "\"perclos\":%.2f,\"pitch\":%.1f,\"freeze\":%s,\"state\":\"%s\",\"lra_hz\":%u}",
        (unsigned long)snap->timestamp_s, snap->ambient_lux,
        snap->pupil_measured_mm, snap->pupil_baseline_mm,
        snap->delta_d_autonomic_mm, snap->cpui_mm_per_min,
        snap->ear_current, snap->blink_rate_per_min,
        snap->perclos_pct, snap->head_pitch_deg,
        snap->motion_gate_freeze ? "true" : "false",
        snap->classified_state, snap->haptic_lra_pwm_hz
    );
}

#endif // OCULOPULSE_CORE_H
