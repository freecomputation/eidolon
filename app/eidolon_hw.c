#include "eidolon_hw.h"

#include <targets/f7/furi_hal/furi_hal_resources.h>

EidolonRadio eidolon_radio;
EidolonAdc eidolon_adc;
EidolonFieldDetect eidolon_fd;

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
