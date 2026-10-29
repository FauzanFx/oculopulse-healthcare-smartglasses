# OculoPulse — Smart Non-Contact Neuro-Visual & Ergonomic Data Collector Glasses

Repositori ini berisi kode sumber *firmware edge* C/C++ (Seeed Studio XIAO ESP32-S3 Sense), *testbench* validasi *Software-in-the-Loop* (SIL), skrip simulasi & pemodelan daya, serta antarmuka aplikasi pendamping (*companion app*) untuk **OculoPulse**.

> **Catatan Status Pengembangan:** Sistem pada repositori ini merupakan purwarupa rancangan rekayasa (*engineering design*) yang divalidasi melalui kompilasi *firmware* C++ dan simulasi *Software-in-the-Loop* (SIL) 4 skenario fisiologis. Pengujian klinis pada perangkat keras fisik di dunia nyata belum dilakukan pada tahap ini.

---

## Struktur Repositori

```text
├── firmware_esp32s3/
│   ├── oculopulse_core.h          # Pustaka inti C/C++ (ekstraksi elips ROI 120x120, PLR baseline, IMU filter, pohon keputusan)
│   ├── oculopulse_firmware.ino    # Firmware FreeRTOS Dual-Core untuk Seeed Studio XIAO ESP32-S3 Sense
│   └── validate_firmware_sil.cpp  # Testbench C++17 untuk audit SRAM, unit test, benchmark 1.000 frame, & simulasi SIL 240 detik
├── simulation/
│   ├── sensor_fusion_engine.py    # Simulator referensi fusi multi-sensor Python (4 skenario @ 60 detik)
│   ├── plot_sil_flat_monochrome.py# Generator grafik verifikasi kompilasi C++ & deret waktu SIL (300 DPI)
│   ├── plot_bab4_flat_monochrome.py# Generator grafik anggaran daya & kurva pengosongan LiPo 350 mAh
│   ├── firmware_linearity_test.csv# Hasil uji linearitas ekstraksi sumbu mayor elips 2a (2,6–6,4 mm)
│   ├── firmware_latency_1000frames.csv # Log waktu eksekusi 1.000 siklus frame ROI 120x120 beruntun
│   ├── telemetry_log_4scenarios.csv    # Log keluarnya telemetri SIL 240 detik (CSV)
│   ├── telemetry_log_4scenarios.json   # Log keluarnya telemetri SIL 240 detik (JSON)
│   └── power_discharge_curve_350mah.csv# Data kurva pengosongan tegangan baterai Curved LiPo 350 mAh
├── dashboard/
│   └── index.html                 # Antarmuka aplikasi pendamping OculoPulse Pro (Web Bluetooth & MQTT Bridge)
└── figures/                       # Grafik hasil verifikasi algoritma, simulasi SIL, dan profil daya (300 DPI)
```

---

## Spesifikasi & Pemetaan Pin Perangkat Keras

| Komponen | Spesifikasi | Antarmuka / Pin XIAO ESP32-S3 Sense |
| :--- | :--- | :--- |
| **Unit Pemroses Utama (MCU)** | Seeed Studio XIAO ESP32-S3 Sense (Dual-Core Xtensa LX7 @ 240 MHz, 512 KB SRAM) | On-board |
| **Sensor Kamera Okular** | OmniVision OV2640 NoIR (160×120 base $\rightarrow$ 120×120 ROI Grayscale @ 15 FPS) | Konektor B2B DVP Kamera |
| **Iluminator NIR & Driver** | LED Vishay VSMY1850 (850 nm) + MOSFET N-Channel AO3400A (Pulsa 8,0 ms, *Duty Cycle* 12%) | `D0` (`GPIO 1`) |
| **Sensor Cahaya Ambien** | Vishay VEML7700-TT (`0x10`) | $\text{I}^2\text{C}$ SDA: `D4` (`GPIO 5`), SCL: `D5` (`GPIO 6`) |
| **Sensor Gerak 6-Axis IMU** | STMicroelectronics LSM6DS3TR-C (`0x6A`) + Interupsi *Wake-on-Motion* | $\text{I}^2\text{C}$ (`GPIO 5/6`) + `INT1`: `D1` (`GPIO 2`) |
| **Aktuator Haptik** | Linear Resonant Actuator (LRA 8 mm, 170 Hz / 200 Hz) | `D3` (`GPIO 4`) |
| **Manajemen Daya & Baterai** | Curved LiPo 3,7 V 350 mAh (`LP501255`) + TI BQ25101 + TI TPS62740 (3,3 V) | ADC Monitor: `D2` (`GPIO 3`) |

---

## Cara Menjalankan Validasi Firmware & Simulasi

### 1. Kompilasi & Eksekusi Testbench Firmware C++ (SIL)
Pastikan kompilator `g++` (mendukung C++17) telah terpasang, lalu jalankan perintah berikut dari direktori utama repositori:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Werror firmware_esp32s3/validate_firmware_sil.cpp -o firmware_esp32s3/validate_firmware_sil
./firmware_esp32s3/validate_firmware_sil
```

Program ini akan menguji pustaka [`oculopulse_core.h`](firmware_esp32s3/oculopulse_core.h) secara langsung dan menghasilkan berkas CSV/JSON pada folder `simulation/`.

### 2. Regenerasi Grafik Verifikasi & Profil Daya
Pastikan `python3`, `numpy`, dan `matplotlib` telah terpasang:

```bash
python3 simulation/plot_sil_flat_monochrome.py
python3 simulation/plot_bab4_flat_monochrome.py
```

Seluruh grafik hasil pengujian akan disimpan secara otomatis di dalam direktori `figures/`.

### 3. Menjalankan Antarmuka Aplikasi Pendamping (Dashboard)
Buka berkas [`dashboard/index.html`](dashboard/index.html) secara langsung menggunakan peramban web (*browser*) modern (Chrome/Edge/Firefox) dan klik tombol **Skenario** untuk menguji respons antarmuka pada ke-4 fase fisiologis.
