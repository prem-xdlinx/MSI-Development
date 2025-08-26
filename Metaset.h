// MetaSet.h
#ifndef METASET_H
#define METASET_H

#include <cstdint>

#pragma pack(push, 1)
struct MetaSet {
    char XDLX[4];   
    char SAT_ID[4];
    uint16_t OrbitNumber;
    uint32_t Task_ID;
    uint32_t ImageStartTime;
    uint8_t  ImagingDuration;
    uint8_t  ConfigAndTDIFile;
    float Latitude;
    float Longitude;
    uint32_t PPSRef;
    uint64_t TimeRef;
    uint64_t TimeCounter;
};
#pragma pack(pop)

#endif // METASET_H
