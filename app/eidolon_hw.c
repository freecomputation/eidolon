#include "eidolon_hw.h"

#include <targets/f7/furi_hal/furi_hal_resources.h>

EidolonRadio eidolon_radio;
EidolonAdc eidolon_adc;
EidolonFieldDetect eidolon_fd;
EidolonIr eidolon_ir;

/* ------------------------------------------------------------------ */
/* Radio                                                               */
/* ------------------------------------------------------------------ */

bool eidolon_radio_start(EidolonApp* app) {
    UNUSED(app);
    if(eidolon_radio.active) return true;
    subghz_devices_init();
    eidolon_radio.device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(!eidolon_radio.device || !subghz_devices_begin(eidolon_radio.device)) {
        subghz_devices_deinit();
        eidolon_radio.device = NULL;
        return false;
    }
    subghz_devices_load_preset(eidolon_radio.device, FuriHalSubGhzPresetOok650Async, NULL);
    eidolon_radio.active = true;
    return true;
}

void eidolon_radio_stop(EidolonApp* app) {
    UNUSED(app);
    if(!eidolon_radio.active) return;
    subghz_devices_set_async_mirror_pin(eidolon_radio.device, NULL);
    subghz_devices_idle(eidolon_radio.device);
    subghz_devices_sleep(eidolon_radio.device);
    subghz_devices_end(eidolon_radio.device);
    subghz_devices_deinit();
    eidolon_radio.device = NULL;
    eidolon_radio.active = false;
}

void eidolon_radio_tune(EidolonApp* app, uint32_t freq_hz) {
    UNUSED(app);
    if(!eidolon_radio.active) return;
    subghz_devices_idle(eidolon_radio.device);
    subghz_devices_set_frequency(eidolon_radio.device, freq_hz);
    subghz_devices_flush_rx(eidolon_radio.device);
    subghz_devices_set_rx(eidolon_radio.device);
}

float eidolon_radio_rssi(EidolonApp* app) {
    UNUSED(app);
    if(!eidolon_radio.active) return -130.0f;
    return subghz_devices_get_rssi(eidolon_radio.device);
}

void eidolon_radio_mirror(EidolonApp* app, bool on) {
    if(!eidolon_radio.active) return;
    subghz_devices_set_async_mirror_pin(eidolon_radio.device, on ? &gpio_speaker : NULL);
    app->mirror_active = on;
}

/* ------------------------------------------------------------------ */
/* ADC                                                                 */
/* ------------------------------------------------------------------ */

static FuriHalAdcChannel eidolon_adc_channel_for(const GpioPin* pin, bool* valid) {
    for(size_t i = 0; i < gpio_pins_count; i++) {
        if(gpio_pins[i].pin == pin) {
            *valid = true;
            return gpio_pins[i].channel;
        }
    }
    *valid = false;
    return FuriHalAdcChannelNone;
}

bool eidolon_adc_start(EidolonApp* app) {
    UNUSED(app);
    if(eidolon_adc.active) return true;
    bool ok0 = false, ok1 = false;
    eidolon_adc.ch_pc0 = eidolon_adc_channel_for(&gpio_ext_pc0, &ok0);
    eidolon_adc.ch_pc1 = eidolon_adc_channel_for(&gpio_ext_pc1, &ok1);
    eidolon_adc.channels_valid = ok0 && ok1;
    if(!eidolon_adc.channels_valid) return false;
    furi_hal_gpio_init(&gpio_ext_pc0, GpioModeAnalog, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(&gpio_ext_pc1, GpioModeAnalog, GpioPullNo, GpioSpeedVeryHigh);
    eidolon_adc.handle = furi_hal_adc_acquire();
    if(!eidolon_adc.handle) return false;
    furi_hal_adc_configure(eidolon_adc.handle);
    eidolon_adc.active = true;
    return true;
}

void eidolon_adc_stop(EidolonApp* app) {
    UNUSED(app);
    if(!eidolon_adc.active) return;
    furi_hal_adc_release(eidolon_adc.handle);
    eidolon_adc.handle = NULL;
    furi_hal_gpio_init(&gpio_ext_pc0, GpioModeInput, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(&gpio_ext_pc1, GpioModeInput, GpioPullNo, GpioSpeedVeryHigh);
    eidolon_adc.active = false;
}

void eidolon_adc_config(EidolonApp* app, FuriHalAdcOversample oversample, FuriHalAdcSamplingTime sampling) {
    UNUSED(app);
    if(!eidolon_adc.active) return;
    furi_hal_adc_configure_ex(
        eidolon_adc.handle, FuriHalAdcScale2048, FuriHalAdcClockSync64, oversample, sampling);
}

uint16_t eidolon_adc_read(EidolonApp* app, FuriHalAdcChannel channel) {
    UNUSED(app);
    if(!eidolon_adc.active) return 0;
    return furi_hal_adc_read(eidolon_adc.handle, channel);
}

uint16_t eidolon_adc_jitter(EidolonApp* app, FuriHalAdcChannel channel, uint8_t count) {
    if(!eidolon_adc.active || count == 0) return 0;
    uint16_t min_v = 0xFFFF, max_v = 0;
    for(uint8_t i = 0; i < count; i++) {
        uint16_t v = eidolon_adc_read(app, channel);
        if(v < min_v) min_v = v;
        if(v > max_v) max_v = v;
    }
    return max_v - min_v;
}

/* ------------------------------------------------------------------ */
/* NFC / RFID field detection (time-multiplexed windows)               */
/* ------------------------------------------------------------------ */

#define FD_WINDOW_MS 600

static void eidolon_fd_window_start(EidolonApp* app) {
    UNUSED(app);
    if(eidolon_fd.window == 0) {
        furi_hal_nfc_field_detect_start();
    } else {
        furi_hal_rfid_field_detect_start();
    }
}

static void eidolon_fd_window_stop(EidolonApp* app) {
    UNUSED(app);
    if(eidolon_fd.window == 0) {
        furi_hal_nfc_field_detect_stop();
    } else {
        furi_hal_rfid_field_detect_stop();
    }
}

void eidolon_fd_start(EidolonApp* app) {
    memset(&eidolon_fd, 0, sizeof(eidolon_fd));
    eidolon_fd.active = true;
    eidolon_fd.window = 0;
    eidolon_fd.switch_tick = furi_get_tick();
    eidolon_fd_window_start(app);
}

void eidolon_fd_stop(EidolonApp* app) {
    if(!eidolon_fd.active) return;
    eidolon_fd_window_stop(app);
    eidolon_fd.active = false;
    app->field_present = false;
}

void eidolon_fd_tick(EidolonApp* app) {
    if(!eidolon_fd.active) return;
    uint32_t now = furi_get_tick();
    if(now - eidolon_fd.switch_tick >= FD_WINDOW_MS) {
        eidolon_fd_window_stop(app);
        eidolon_fd.window = (eidolon_fd.window + 1) % 2;
        eidolon_fd.switch_tick = now;
        eidolon_fd_window_start(app);
    }
    if(eidolon_fd.window == 0) {
        eidolon_fd.nfc_present = furi_hal_nfc_field_is_present();
        if(eidolon_fd.nfc_present) eidolon_fd.nfc_seen_tick = now;
    } else {
        uint32_t timing = 0;
        eidolon_fd.rfid_present = furi_hal_rfid_field_is_present(&timing);
        if(eidolon_fd.rfid_present) eidolon_fd.rfid_seen_tick = now;
    }
    bool recent = eidolon_fd_recent(app, 2000);
    if(recent && !app->field_present) {
        app->field_present_tick = now;
        eidolon_log_event(app, "FIELD", "reader field present (13.56MHz/125kHz)");
    }
    app->field_present = recent;
}

bool eidolon_fd_recent(EidolonApp* app, uint32_t within_ms) {
    UNUSED(app);
    if(!eidolon_fd.active) return false;
    uint32_t now = furi_get_tick();
    if(eidolon_fd.nfc_seen_tick && (now - eidolon_fd.nfc_seen_tick) < within_ms) return true;
    if(eidolon_fd.rfid_seen_tick && (now - eidolon_fd.rfid_seen_tick) < within_ms) return true;
    return false;
}

/* ------------------------------------------------------------------ */
/* Battery                                                             */
/* ------------------------------------------------------------------ */

float eidolon_battery_temp(void) {
    return furi_hal_power_get_battery_temperature(FuriHalPowerICFuelGauge);
}

/* ------------------------------------------------------------------ */
/* Infrared watch                                                      */
/* ------------------------------------------------------------------ */

static void eidolon_ir_callback(void* context, InfraredWorkerSignal* signal) {
    EidolonApp* app = context;
    EidolonIrEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.tick = furi_get_tick();
    if(infrared_worker_signal_is_decoded(signal)) {
        ev.decoded = true;
        const InfraredMessage* msg = infrared_worker_get_decoded_signal(signal);
        if(msg) ev.msg = *msg;
    } else {
        const uint32_t* timings = NULL;
        size_t count = 0;
        infrared_worker_get_raw_signal(signal, &timings, &count);
        ev.timings_count = (uint32_t)count;
    }
    uint8_t next = (eidolon_ir.head + 1) % EIDOLON_IR_RING;
    if(next != eidolon_ir.tail) {
        eidolon_ir.ring[eidolon_ir.head] = ev;
        eidolon_ir.head = next;
    }
    app->ir_events_total++;
    app->ir_last_tick = ev.tick;
}

void eidolon_ir_start(EidolonApp* app) {
    if(eidolon_ir.active) return;
    memset(&eidolon_ir, 0, sizeof(eidolon_ir));
    eidolon_ir.worker = infrared_worker_alloc();
    infrared_worker_rx_set_received_signal_callback(eidolon_ir.worker, eidolon_ir_callback, app);
    infrared_worker_rx_enable_signal_decoding(eidolon_ir.worker, true);
    infrared_worker_rx_enable_blink_on_receiving(eidolon_ir.worker, false);
    infrared_worker_rx_start(eidolon_ir.worker);
    eidolon_ir.active = true;
}

void eidolon_ir_stop(EidolonApp* app) {
    UNUSED(app);
    if(!eidolon_ir.active) return;
    infrared_worker_rx_stop(eidolon_ir.worker);
    infrared_worker_free(eidolon_ir.worker);
    eidolon_ir.worker = NULL;
    eidolon_ir.active = false;
}

bool eidolon_ir_poll(EidolonApp* app, EidolonIrEvent* out) {
    UNUSED(app);
    if(eidolon_ir.tail == eidolon_ir.head) return false;
    *out = eidolon_ir.ring[eidolon_ir.tail];
    eidolon_ir.tail = (eidolon_ir.tail + 1) % EIDOLON_IR_RING;
    return true;
}

void eidolon_ir_log_event(EidolonApp* app, const char* tag, const EidolonIrEvent* ev) {
    if(ev->decoded) {
        eidolon_log_event(
            app,
            tag,
            "IR %s addr 0x%lX cmd 0x%lX%s",
            infrared_get_protocol_name(ev->msg.protocol),
            (unsigned long)ev->msg.address,
            (unsigned long)ev->msg.command,
            ev->msg.repeat ? " (repeat)" : "");
    } else {
        eidolon_log_event(app, tag, "IR raw burst, %lu timings", (unsigned long)ev->timings_count);
    }
}
