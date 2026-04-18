#pragma once

#include "state.h"

#ifdef ARDUINO

void initDisplay();
void drawAllRegions(const UiState& uiState);

void drawSalahNameRegion(const UiState& uiState);
void drawMinutesRegion(const UiState& uiState);
void drawSensorRowRegion(const UiState& uiState, int rowIndex);
void drawForecastRegion(const UiState& uiState);

void updateSalahNameRegion();
void updateMinutesRegion();
void updateSensorRowRegion(int rowIndex);
void updateForecastRegion();

#endif
