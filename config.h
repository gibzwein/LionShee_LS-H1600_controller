#pragma once

// Calculation limits
const float MAX_VALID_ACTIVE_POWER = 4000.0;
const int MAX_POWER = 650;

// Battery SOC override
const int SOC_FORCE_THRESHOLD = 97;
const int SOC_FORCE_POWER = 650;

// Main polling interval
const unsigned long POLL_INTERVAL_MS = 60000;

// Display
const int SCREEN_WIDTH = 320;
const int SCREEN_HEIGHT = 240;
const int LEFT_X = 10;
const int RIGHT_X = 310;

const char* POLISH_TIME_ZONE = "CET-1CEST,M3.5.0,M10.5.0/3";