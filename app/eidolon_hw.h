#pragma once

#include "eidolon.h"

#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/infrared/worker/infrared_worker.h>

/* ------------------------------------------------------------------ */
/* CC1101 internal radio (Sub-GHz)                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    bool active;
    const SubGhzDevice* device;
} EidolonRadio;

bool eidolon_radio_start(EidolonApp* app);
void eidolon_radio_stop(EidolonApp* app);
void eidolon_radio_tune(EidolonApp* app, uint32_t freq_hz);
float eidolon_radio_rssi(EidolonApp* app);
void eidolon_radio_mirror(EidolonApp* app, bool on);

/* ------------------------------------------------------------------ */
/* ADC (REM wire on pin 15 / PC1, Hall sensor on pin 16 / PC0)         */
/* ------------------------------------------------------------------ */

typedef struct {
    bool active;
    FuriHalAdcHandle* handle;
    FuriHalAdcChannel ch_pc0; /* pin 16 */
    FuriHalAdcChannel ch_pc1; /* pin 15 */
    bool channels_valid;
} EidolonAdc;

bool eidolon_adc_start(EidolonApp* app);
void eidolon_adc_stop(EidolonApp* app);
void eidolon_adc_config(EidolonApp* app, FuriHalAdcOversample oversample, FuriHalAdcSamplingTime sampling);
uint16_t eidolon_adc_read(EidolonApp* app, FuriHalAdcChannel channel);

/* Peak-to-peak spread of `count` back-to-back raw reads (REM pod method). */
uint16_t eidolon_adc_jitter(EidolonApp* app, FuriHalAdcChannel channel, uint8_t count);

/* ------------------------------------------------------------------ */
/* NFC / 125 kHz reader-field detection, time-multiplexed              */
/* ------------------------------------------------------------------ */

typedef struct {
    bool active;
    uint8_t window; /* 0 = NFC, 1 = RFID */
    uint32_t switch_tick;
    bool nfc_present;
    bool rfid_present;
    uint32_t nfc_seen_tick;
    uint32_t rfid_seen_tick;
} EidolonFieldDetect;

void eidolon_fd_start(EidolonApp* app);
void eidolon_fd_stop(EidolonApp* app);
void eidolon_fd_tick(EidolonApp* app);
bool eidolon_fd_recent(EidolonApp* app, uint32_t within_ms);

/* Battery (fuel gauge) temperature in degrees C. */
float eidolon_battery_temp(void);

/* ------------------------------------------------------------------ */
/* Infrared watch (dark watch / fusion)                                */
/* ------------------------------------------------------------------ */

#define EIDOLON_IR_RING 6

typedef struct {
    bool decoded;
    InfraredMessage msg;
    uint32_t timings_count;
    uint32_t tick;
} EidolonIrEvent;

typedef struct {
    bool active;
    InfraredWorker* worker;
    EidolonIrEvent ring[EIDOLON_IR_RING];
    volatile uint8_t head; /* producer: worker thread */
    uint8_t tail; /* consumer: main thread */
} EidolonIr;

extern EidolonIr eidolon_ir;

void eidolon_ir_start(EidolonApp* app);
void eidolon_ir_stop(EidolonApp* app);
/* Main-thread drain: returns the next queued event, if any. */
bool eidolon_ir_poll(EidolonApp* app, EidolonIrEvent* out);
/* Log one IR event under the given tag. */
void eidolon_ir_log_event(EidolonApp* app, const char* tag, const EidolonIrEvent* ev);

/* ------------------------------------------------------------------ */
/* Shared hardware instances (owned by eidolon_hw.c)                   */
/* ------------------------------------------------------------------ */

extern EidolonRadio eidolon_radio;
extern EidolonAdc eidolon_adc;
extern EidolonFieldDetect eidolon_fd;
