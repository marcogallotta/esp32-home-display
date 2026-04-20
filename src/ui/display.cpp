#include "display.h"
#include "state.h"

#ifdef ARDUINO

#include <U8g2lib.h>
#include <SPI.h>

#include "../salah/service.h"

namespace {
U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI oled(
    U8G2_R0,
    /* cs=*/    10,
    /* dc=*/     9,
    /* reset=*/  8
);

constexpr int kMaxVisibleSensorRows = 4;

void clearRegion(int x, int y, int w, int h) {
    oled.setDrawColor(0);
    oled.drawBox(x, y, w, h);
    oled.setDrawColor(1);
}

bool shouldFlipColoursForSalah(const salah::State& salah) {
    if (salah.current == salah::Phase::Fajr) {
        return true;
    }
    if (salah.current == salah::Phase::AsrMakruh && salah.minutesRemaining <= 10) {
        return true;
    }
    return false;
}
} // namespace

void initDisplay() {
    SPI.begin(/* sck=*/ 12, /* miso=*/ -1, /* mosi=*/ 11, /* ss=*/ 10);
    oled.setBusClock(8000000);
    oled.setFont(u8g2_font_7x14_tf);
    oled.begin();
}

void drawSalahNameRegion(const State& state) {
    clearRegion(0, 0, 48, 16);
    if (state.hasSalah) {
        if (shouldFlipColoursForSalah(state.salah)) {
            oled.setDrawColor(1);
            oled.drawBox(0, 0, 48, 16);
            oled.setDrawColor(0);
        }
        oled.drawStr(0, 12, toShortString(state.salah.current));
        oled.setDrawColor(1);

        if (state.salah.current == salah::Phase::Isha) {
            oled.setContrast(0x00);
        } else {
            oled.setContrast(0xff);
        }
    }
}

void drawMinutesRegion(const State& state) {
    clearRegion(0, 16, 48, 16);
    if (state.hasSalah) {
        if (shouldFlipColoursForSalah(state.salah)) {
            oled.setDrawColor(1);
            oled.drawBox(0, 16, 48, 16);
            oled.setDrawColor(0);
        }
        char buf[32];
        const int min = state.salah.minutesRemaining;
        if (min > 60) {
            std::snprintf(buf, sizeof(buf), "%dh %d", min / 60, min % 60);
        } else {
            std::snprintf(buf, sizeof(buf), "%d m", min);
        }
        oled.drawStr(0, 28, buf);
        oled.setDrawColor(1);
    }
}

void drawSensorRowRegion(const State& state, int rowIndex) {
    const int x = 48;
    const int y = rowIndex * 16;
    const int w = 44;
    const int h = 16;

    clearRegion(x, y, w, h);

    const SensorRowState& row = state.sensors[rowIndex];
    if (!row.hasReading) {
        return;
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%c", row.shortName);
    oled.drawStr(x, y + 12, buf);
    std::snprintf(buf, sizeof(buf), "%d", displayTemp(row.temperatureC));
    oled.drawStr(x + 12, y + 12, buf);
    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(row.humidity));
    oled.drawStr(x + 28, y + 12, buf);
}

void drawForecastRegion(const State& state) {
    clearRegion(92, 0, 36, 64);
    if (!state.hasForecast) {
        return;
    }

    for (int i = 0; i < state.forecast.count; ++i) {
        const auto& day = state.forecast.days[i];
        char buf[32];
        std::snprintf(
            buf,
            sizeof(buf),
            "%d/%d",
            static_cast<int>(day.tempMin),
            static_cast<int>(day.tempMax)
        );
        oled.drawStr(92, 12 + i * 16, buf);
    }
}

void drawAllRegions(const State& state) {
    drawSalahNameRegion(state);
    drawMinutesRegion(state);

    const int visibleRows = std::min<int>(
        static_cast<int>(state.sensors.size()),
        kMaxVisibleSensorRows
    );
    for (int rowIndex = 0; rowIndex < visibleRows; ++rowIndex) {
        drawSensorRowRegion(state, rowIndex);
    }

    drawForecastRegion(state);

    oled.sendBuffer();
}

void updateSalahNameRegion() {
    oled.updateDisplayArea(0, 0, 6, 2);
}

void updateMinutesRegion() {
    oled.updateDisplayArea(0, 2, 6, 2);
}

void updateSensorRowRegion(int rowIndex) {
    oled.updateDisplayArea(6, rowIndex * 2, 6, 2);
}

void updateForecastRegion() {
    oled.updateDisplayArea(11, 0, 5, 8);
}

#endif
