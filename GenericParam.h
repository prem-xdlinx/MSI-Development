#ifndef GENERICPARAM_H
#define GENERICPARAM_H

#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#include "stdafx.h"
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <limits>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cinttypes>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <arpa/inet.h>
#include <cerrno>
#include <ifaddrs.h>
#include <net/if.h>
#include <sysexits.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <cstddef>
#include <cstdlib>
#include <unistd.h>
#include <iterator>
#include <time.h>
#include <cstdint>
#include <unordered_map>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <nlohmann/json.hpp> 
#include <mutex>
#include <signal.h>
#include <atomic>
#include <future>
using json = nlohmann::json;
using namespace std::chrono;
using namespace std;
#include "KYFGLib.h"
#include "sharedParams.h"


#define TIME
#define BINNING
// #define PRINT_FILE
#define TDIYSHIFT
#define ERRORCODE
#define CONTENT
#define METAFILE


#ifdef PRINT_FILE

#define PRINT_MESSAGE ProcessLogFile
#else
#define PRINT_MESSAGE	std::cout
#endif


#define PRINT_LOG(code, msg_expr)                   \
    do {                                      \
        std::ostringstream oss__;             \
        oss__ << msg_expr;                    \
        printLog(code, oss__);          \
    } while (0)


#include <fstream>

extern std::ofstream ProcessLogFile;

#define Max_File_Path_Len   1000
#define Max_Curr_Path_Len   512
#define MAX_PARAM_LEN       256
#define KY_MAX_BOARDS       4//Maximum no of grabber Cards support
#define MAX_STREAM_BUFFER   64//Maximum no of Frames can be allocated per stream
//#define KY_MAX_CAMERAS

#define MIN_TIME_TH_MSEC 5000   //Minimum time Gap between Acquisition to system time in milliseconds
#define MAX_TIME_TH_MSEC 180000 //Maximum time Gap between Acquisition to system time in milliseconds
#define MAX_DURATION 20 //Maximum Duration in seconds
#define ADDED_DELAY      4  //seconds
#define MAX_GAIN 7.99923
#define MAX_BAND_SHIFT 64
#define MIN_BAND_SHIFT -64
#define OVERLAP 2
#if !defined(_countof)
#define _countof(_Array) (sizeof(_Array) / sizeof(_Array[0]))
#endif
extern char ArguementsProcessed[MAX_PARAM_LEN];
// extern char acquisitionPath[Max_Curr_Path_Len];
extern std::string acquisitionPath;
// extern struct MetaSet MetaConfig;
#include "Metaset.h"
extern MetaSet MetaConfig;
extern struct FileParam Param;
extern int maxBufferStream;
extern int64_t dmaImageIdCapable;
extern int64_t TotalFrames;
extern bool CaptureCompleted;
extern unsigned int currentGrabberIndex;
//unsigned int currentGrabberIndex;
extern int printCxp2Events;
extern int printHeartbeats;

bool NumberInRangeInput(int min, int max, int* value, const char* error_str);
//#define MINIMAL_CALLBACK //Uncomment this line for Minimal Callback information
//#define FGLIB_ALLOCATED_BUFFERS // Uncomment this to use buffers allocated by KYFGLib



// Common Parameters
struct GlobalParams{
    int         Width;
    int         Height;
    int         bandHeight;
    int         TDIMode;
    int         TDIStages;
    int         regionHeight;
    size_t      totalFrames;
    std::vector<uint8_t> regionModes;
    std::vector<uint8_t> binningStatus;
    double     FPS;
    double     ExpTime;
    double     Gain;
    int        BandXShift;
    int     CoreTemperature;
    float     SensorTemperature;
    // int        BandYShift;
};
extern GlobalParams commonParams;
//Timestamp Calculation
extern std::chrono::system_clock::time_point now;
extern long long presentTime;
extern long long previousTime;

inline void timeStampCalculate(){
    now = std::chrono::system_clock::now();
    presentTime = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    PRINT_MESSAGE<<"["<<presentTime - previousTime<<"] ";
    previousTime = presentTime;
}
//User Input
extern int G_TotArg;
extern int G_Task;
extern std::tm G_Time; //Current Time + 10Sec
extern float G_FPS;//Maximum FPS
extern float G_Duration;//seconds
extern int G_Nframes;
extern int G_TDIYShift;
extern unsigned char G_Band;//All Bands
extern unsigned char G_TDI;//No TDI
extern unsigned char G_JSON;

//Derived Global Varriable
extern unsigned char G_TDI_Stage;
extern unsigned char G_TDI_Modes;

//Added on 22.03.25
extern int G_OrbitNo;
extern float G_FPS;//Added on 22.03.2025
extern float G_ExpTime;
extern float G_Gain;
extern int G_BandXShift;
// extern int G_BandYShift;
extern unsigned char G_Bin;
extern unsigned char G_Binning;
extern bool G_CCSDSProcessStatus;
extern int G_ReadNlines;
inline bool MSIAPP = false; //MSI Application Flag




struct CameraStatus {
    uint32_t cameraStatus: 2; // 2 bits for Camera Status
    uint32_t framesCaptured: 14; // 14 bits for Frames Captured
    uint32_t GrabberStatus: 16; // 16 bits reserved for future use
    // Constructor to initialize the struct
    CameraStatus() : cameraStatus(0), framesCaptured(0), GrabberStatus(0) {}

    uint32_t toUint32() const {
        uint32_t result = 0;
        result |= (cameraStatus & 0x3);
        result |= (framesCaptured & 0x3FFF) << 2;
        result |= (GrabberStatus & 0xFFFF) << 16;
        return result;
    }
    
};

struct StorageStatus {
    uint32_t payloadSeq : 2; // Defines which seq is executing
    uint32_t storageStatus : 14; // Defines the storage status
    uint32_t secondsCounter : 16; // 16 bits for Seconds Time Counter
    // Constructor to initialize the struct
    StorageStatus() : payloadSeq(1), storageStatus(0), secondsCounter(0) {}
    uint32_t toUint32() const {
        uint32_t result = 0;
        result |= (payloadSeq & 0x3);
        result |= (storageStatus & 0x3FFF) << 2;
        result |= (secondsCounter & 0xFFFF) << 16;
        return result;
    }
};

struct SoftwareFirmStatus {
    uint32_t firmwareVer : 16; // Firmware Version
    uint32_t softwareVer :16; // Software Version
    SoftwareFirmStatus() : firmwareVer(222), softwareVer(6) {}
    uint32_t toUint32() const {
        uint32_t result = 0;
        result |= (firmwareVer & 0xFFFF);
        result |= (softwareVer & 0xFFFF) << 16;
        return result;
    }
};

struct DeviceStatus{
    uint32_t deviceStatus :16; // Device Status
    uint32_t Duration : 16; // Duration in milliseconds
    DeviceStatus() : deviceStatus(0), Duration(0) {}
    uint32_t toUint32() const {
        uint32_t result = 0;
        result |= (deviceStatus & 0xFFFF);
        result |= (Duration & 0xFFFF) << 16;
        return result;
    }
};

struct StartTime{
    uint32_t startTime : 32; // Start time in seconds since epoch
};

struct TelemetryInfo{
    CameraStatus cameraInfo;
    StorageStatus storageInfo;
    DeviceStatus deviceInfo;
    SoftwareFirmStatus softwateFirmInfo;
    StartTime startTime; // Placeholder for start time, if needed
};

extern TelemetryInfo latestTelemetry; // Global telemetry information

inline bool g_resetLogTimer = false;

inline void requestLogTimerReset() {
    g_resetLogTimer = true;
}


//Timestamp Calculation
inline void printLog(const std::string& code, const std::ostringstream& stream) {
    static bool previousEndedWithNewline = true;
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using DurationMs = std::chrono::duration<double, std::milli>;

    static TimePoint startTime = Clock::now();
    static TimePoint lastTime = startTime;

      // Reset statics if requested
    if (g_resetLogTimer) {
        startTime = Clock::now();
        lastTime = startTime;
        previousEndedWithNewline = true;
        g_resetLogTimer = false;
    }

    TimePoint currentTime = Clock::now();
    DurationMs totalElapsed = currentTime - startTime;
    //DurationMs deltaElapsed = currentTime - lastTime;
    std::string content = stream.str();
#ifdef TIME
    
    if(previousEndedWithNewline){
        // ...existing code...
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(totalElapsed).count();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(totalElapsed).count();

    int sec = ms / 1000;
    int millisecond = ms % 1000;
    int microSec = us % 1000;

    PRINT_MESSAGE << "[" << std::setw(2) << std::setfill('0') << sec << ","
                << std::setw(3) << millisecond << "."
                << std::setw(3) << microSec << "]";
    }
#endif
#ifdef ERRORCODE
    if (code.empty()) {
        PRINT_MESSAGE << "";
    } else {
        PRINT_MESSAGE << code << " ";
    }
#endif
#ifdef CONTENT
   PRINT_MESSAGE << content;
#endif
//    PRINT_MESSAGE.flush();  // Flush to disk immediately

    previousEndedWithNewline = !content.empty() && content.back() == '\n';
    //if(previousEndedWithNewline){
    //    lastTime = currentTime;
    //}

    latestTelemetry.storageInfo.secondsCounter = static_cast<uint32_t>(totalElapsed.count() / 1000); // Update seconds counter in telemetry
}




//////////////////////// Stream Class Info /////////////////////////////////////
#pragma pack(push, 1)
typedef struct _METADATA_CHUNKS
{
    char XDLX[4];
    char SAT_ID[4];
    uint16_t OrbitNumber;
    uint32_t Task_ID;
    uint32_t ImageStartTime;
    uint8_t ImagingDuration;
    uint8_t ConfigAndTDIFile;
    float Latitude;
    float Longitude;
    uint32_t PPSRef;
    uint64_t TimeRef;
    uint64_t TimeCounter;
    uint8_t bandsUsed;
    uint8_t band1Active;
    uint8_t band2Active;
    uint8_t band3Active;
    uint8_t band4Active;
    uint8_t band5Active;
    uint8_t band6Active;
    uint8_t band7Active;

} METADATA_CHUNKS;
#pragma pack(pop)

struct FileParam
{
    int fd;
#ifdef METAFILE
    FILE *fpMeta;
#endif
    char *file_memory;
    size_t file_size;
    size_t offset;
    uint64_t CaptureCount;
    uint32_t MetaStartPos;
    uint64_t timestamp;
    uint64_t lastTimestamp = 0;
    double TimeDifference = 0;
    METADATA_CHUNKS metadataChunks;
    int  framedrops = 0;
    CAMHANDLE CamHandle;
    uint64_t iFrames = 0;
};

extern FileParam Param; //[7];

//////////////////////////////// End //////////////////////////////////////
#endif
