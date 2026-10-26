#pragma once
#include <cmath>
#include <stdint.h>

// Snapshot sensor dan event matang di NVS. Naikkan schema jika format diubah.
struct Sample {
  uint32_t schema=2, boot=0, sequence=0;
  uint64_t uptimeMs=0;
  int64_t epoch=0;
  float temperature=NAN, humidity=NAN, error=NAN;
  float mq135V=NAN, mq3V=NAN, phV=NAN;
  float alcohol=NAN, ph=NAN, mq135Ppm=NAN;
  float fan=0, heater=0, rawFan=NAN, rawHeater=NAN;
  bool temperatureValid=false, humidityValid=false, mq3Valid=false, phValid=false;
  bool mq135Valid=false, eligible=false, mature=false;
  bool mq3Calibrated=false, phCalibrated=false, andProduct=true;
  uint32_t drops=0;
  char state[32]="STARTING";
};
