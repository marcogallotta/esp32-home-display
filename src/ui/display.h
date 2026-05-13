#pragma once

#include "../state.h"
#include "state.h"

#ifdef ARDUINO

void initDisplay();
void drawAllRegions(const State& state);

void drawSalahNameRegion(const State& state);
void drawMinutesRegion(const State& state);
void drawSensorRowRegion(const State& state, int rowIndex);
void drawForecastRegion(const State& state);

void updateSalahNameRegion();
void updateMinutesRegion();
void updateSensorRowRegion(int rowIndex);
void updateForecastRegion();

#endif

void renderUi(const State& state, const UiState& uiState, bool doFullDraw);
