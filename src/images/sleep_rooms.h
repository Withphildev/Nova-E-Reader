#pragma once
#include <cstdint>
#include "images/NovaSleep.h"

// Sleep Room Stage stubs (pointing to the original sleep screen image for v1)
static const uint8_t* const SleepRoomEmpty = NovaSleepImage;       // Stage 1 (Day 1-29)
static const uint8_t* const SleepRoomBookshelf = NovaSleepImage;   // Stage 2 (Day 30-99)
static const uint8_t* const SleepRoomComfortable = NovaSleepImage; // Stage 3 (Day 100-364)
static const uint8_t* const SleepRoomLibrary = NovaSleepImage;     // Stage 4 (Day 365+)
