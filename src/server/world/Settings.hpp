#pragma once

#include <string>

#define SETTINGS_NUM 1

struct Settings {
    unsigned short port;
};


bool allSettingsReceived();

extern bool allSettingsReceivedArray[SETTINGS_NUM];
extern Settings settings;
