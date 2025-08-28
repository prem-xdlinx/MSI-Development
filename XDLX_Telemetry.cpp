#include "GenericParam.h"
#include "XDLX_Telemetry.h"


Telemetry::Telemetry() {}
Telemetry::~Telemetry() {}

PayloadMetricsResponse TelemetryData; // Global telemetry data response

std::thread telemetryThread;
std::atomic<bool> running{false};

bool Telemetry::initialize(FGHANDLE fgHandle, CAMHANDLE CamHandle) {
    // Initialize telemetry parameters
    PRINT_LOG("", "Initializing telemetry..." << std::endl);

    running = true;
    telemetryThread = std::thread([this, fgHandle, CamHandle]() {
        while (running) {
            fetchDeviceStatus(fgHandle, CamHandle);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Poll every second
        }
    });

    return true;
}

std::string cameraStatusToString(uint8_t status) {
    switch (status) {
        case 0: return "Fault";
        case 1: return "Ready";
        case 2: return "Waiting";
        case 3: return "Capturing";
        default: return "Unknown";
    }
}

void printCameraStatus(uint32_t packedValue) {
    uint16_t grabberStatus  = (packedValue >> 16) & 0xFFFF;   // bits 16-31
    uint16_t framesCaptured = (packedValue >> 2)  & 0x3FFF;   // bits 2-15
    uint8_t  cameraStatus   =  packedValue        & 0x3;      // bits 0-1

    std::cout << "=== Camera Status ===\n";
    std::cout << "Grabber Status : " << grabberStatus << "\n";
    std::cout << "Frames Captured: " << framesCaptured << "\n";

    std::cout << "Camera Status  : " << static_cast<int>(cameraStatus)
              << " (" << cameraStatusToString(cameraStatus) << ")\n";
}

void printStorageStatus(uint32_t packedValue) {
    uint16_t secondsCounter = (packedValue >> 16) & 0xFFFF;   // bits 16-31
    uint16_t storageStatus  = (packedValue >> 2)  & 0x3FFF;   // bits 2-15
    uint8_t  payloadSeq     =  packedValue        & 0x3;      // bits 0-1

    std::cout << "=== Storage Status ===\n";
    std::cout << "Payload Sequence : " << static_cast<int>(payloadSeq) << "\n";
    std::cout << "Storage Status   : " << storageStatus << "\n";
    std::cout << "Seconds Counter  : " << secondsCounter << " s\n";
}

void printDeviceStatus(uint32_t packedValue) {
    uint16_t duration = (packedValue >> 16) & 0xFFFF; // bits 16-31
    uint16_t deviceStatus     = packedValue & 0xFFFF;         // bits 0-15

    std::cout << "=== Device Status ===\n";
    std::cout << "Device Status : " << deviceStatus << "\n";
    std::cout << "Duration      : " << duration << " ms\n";
}

void printSoftwareFirmStatus(uint32_t packedValue) {
    uint16_t firmwareVer = packedValue & 0xFFFF;          // bits 0-15
    uint16_t softwareVer = (packedValue >> 16) & 0xFFFF;  // bits 16-31

    std::cout << "=== Software and Firmware Status ===\n";
    std::cout << "Firmware Version: " << firmwareVer << "\n";
    std::cout << "Software Version : " << softwareVer << "\n";
}

void Telemetry::fetchDeviceStatus(FGHANDLE fgHandle, CAMHANDLE CamHandle) {

    FGSTATUS status = KYFG_SetCameraValueFloat(Param.CamHandle, "ChunkLatitude", latest_gnss_eph_data.adcs_eph_data.latitude);
    // if(status == FGSTATUS_OK) std::cout<<"\nSet lat success"<<std::endl; else std::cout<<"\nFailed to set lat: "<<std::hex<<status<<std::dec<<std::endl;
    status = KYFG_SetCameraValueFloat(Param.CamHandle, "ChunkLongitude", latest_gnss_eph_data.adcs_eph_data.longitude);
    // if(status == FGSTATUS_OK) std::cout<<"Set lon success"<<std::endl; else std::cout<<"Failed to set lon: "<<std::hex<<status<<std::dec<<std::endl;

    if (Param.CaptureCount > 1){
        latestTelemetry.cameraInfo.cameraStatus = 3;
    }
    latestTelemetry.cameraInfo.framesCaptured = Param.CaptureCount;


    auto now = std::chrono::system_clock::now();
    auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    // std::cout << "From PA: Current Epoch Time (ms): " << epoch_ms << std::endl;
    TelemetryData.timestamp = epoch_ms; // Store current epoch time in milliseconds


    // Parameter 1 contains firmware version and software version (LSB = firmware, MSB = software)
    strncpy(TelemetryData.metrics[0].names, "SoftwareFirmStatus", sizeof(TelemetryData.metrics[0].names) - 1);
    // TelemetryData.metrics[0].names[sizeof(TelemetryData.metrics[0].names) - 1] = '\0'; // Null-terminate
    TelemetryData.metrics[0].counter = latestTelemetry.softwateFirmInfo.toUint32(); // Example firmware version
    // std::cout << "From PA: FirmwareStatus: " << TelemetryData.metrics[0].counter << std::endl;
    // printSoftwareFirmStatus(TelemetryData.metrics[0].counter);

    // Parameter 2 contains device status and duration (LSB = device status, MSB = duration in milliseconds)
    uint32_t stringSize = 0;
    status = KYFG_GetGrabberValueStringCopy(fgHandle, "DeviceStatus", nullptr, &stringSize);
    char* pvalue_str = (char*)malloc(stringSize);
    status = KYFG_GetGrabberValueStringCopy(fgHandle, "DeviceStatus", pvalue_str, &stringSize);
    latestTelemetry.deviceInfo.deviceStatus = status; 
    // strncpy(TelemetryData.metrics[1].names, "DeviceStatus", sizeof(TelemetryData.metrics[1].names) - 1);
    // TelemetryData.metrics[0].names[sizeof(TelemetryData.metrics[0].names) - 1] = '\0'; // Null-terminate
    // if (strcmp(pvalue_str, "Ok") == 0) {
    //     std::cout <<"From PA: DeviceStatus: " << pvalue_str << std::endl;
        TelemetryData.metrics[1].counter = latestTelemetry.deviceInfo.toUint32(); // Assuming 1 means OK
        // std::cout << "From PA: DeviceStatus: " << TelemetryData.metrics[1].counter << std::endl;
    // } else { 
    //     std::cout << "From PA DeviceStatus: Not OK " << pvalue_str << std::endl;
    //     TelemetryData.metrics[0].counter = 0; // Assuming 0 means not OK
    // }
    free(pvalue_str);
    // printDeviceStatus(TelemetryData.metrics[1].counter);
    

    // Parameter 3 contains sensor temperature
    strncpy(TelemetryData.metrics[2].names, "SensorTemp", sizeof(TelemetryData.metrics[2].names) - 1);
    // TelemetryData.metrics[2].names[sizeof(TelemetryData.metrics[2].names) - 1] = '\0'; // Null-terminate
    FGSTATUS Temp = KYFG_SetCameraValueEnum(CamHandle, "DeviceTemperatureSelector", 0);
    if(Temp!=FGSTATUS_OK){
        // std::cout << "Failed to set API for SensorTemp"<<endl;
    }
    double SensorTemp = KYFG_GetCameraValueFloat(CamHandle, "DeviceTemperature");
    TelemetryData.metrics[2].counter = floor(SensorTemp*1000); // Store temperature as counter
    // std::cout << "From PA: SensorTemp: "<<TelemetryData.metrics[2].counter<<endl;


    // Parameter 4 contains device temperature
    strncpy(TelemetryData.metrics[3].names, "DeviceTemp", sizeof(TelemetryData.metrics[3].names) - 1);
    // TelemetryData.metrics[2].names[sizeof(TelemetryData.metrics[2].names) - 1] = '\0'; // Null-terminate
    // Getting value of 'DeviceTemperature'
    int DeviceTemp = KYFG_GetGrabberValueInt(fgHandle, "DeviceTemperature");
    TelemetryData.metrics[3].counter = DeviceTemp; // Store temperature as counter
    // std::cout << "From PA: DeviceTemp: " << TelemetryData.metrics[3].counter << std::endl;

    
    //Camera Status ---> 16bits = Grabber Status --->14bits = FramesCaptured(0-16383) ------->  2bits = CameraStatus( 0 = Fault, 1 = Ready, 2 = Waiting, 3 = Capturing)
    strncpy(TelemetryData.metrics[4].names, "Camera Status", sizeof(TelemetryData.metrics[4].names) - 1);
    TelemetryData.metrics[4].counter = latestTelemetry.cameraInfo.toUint32(); // camera status,
    // std::cout << "From PA: CameraStatus: " <<  TelemetryData.metrics[4].counter << std::endl;
    // printCameraStatus(TelemetryData.metrics[4].counter);


    //Storage Status ----> 16bits = Seconds Counter -----> 14bits = StorageStatus(0-16383)  ----> 2bits = Payload SeqID  
    strncpy(TelemetryData.metrics[5].names, "Storage Info", sizeof(TelemetryData.metrics[5].names) - 1);
    TelemetryData.metrics[5].counter = latestTelemetry.storageInfo.toUint32(); // storage status
    // std::cout << "From PA: StorageInfo: " << TelemetryData.metrics[5].counter << std::endl;
    // printStorageStatus(TelemetryData.metrics[5].counter);


    strncpy(TelemetryData.metrics[6].names, "EPS Voltage", sizeof(TelemetryData.metrics[6].names) - 1);
    TelemetryData.metrics[6].counter = floor(epsVoltage * 1000); // Store EPS voltage as counter
    // std::cout << "From PA: EPS Voltage: " << TelemetryData.metrics[6].counter << std::endl;


    // Parameter Contains Application start time
    strncpy(TelemetryData.metrics[7].names, "StartTime", sizeof(TelemetryData.metrics[7].names) - 1);
    TelemetryData.metrics[7].counter = latestTelemetry.startTime.startTime; // Store start time in seconds since epoch
    // std::cout << "From PA: StartTime: " << TelemetryData.metrics[7].counter << std::endl;
    TelemetryData.used_counter = 8; // Update used counter to 7 for the three metrics

}

void Telemetry::terminate() {
    PRINT_LOG("", "Terminating telemetry..." << std::endl);
    running = false;
    if (telemetryThread.joinable()) {
        telemetryThread.join();
    }
    std::cout << "Telemetry terminated." << std::endl;
    Param.CaptureCount = 0; // Reset capture count
    latestTelemetry = TelemetryInfo(); // Reset telemetry info
    latestTelemetry.cameraInfo = CameraStatus();
    latestTelemetry.storageInfo = StorageStatus();
    latestTelemetry.deviceInfo = DeviceStatus();
    latestTelemetry.softwateFirmInfo = SoftwareFirmStatus();
    TelemetryData.metrics[0].counter = latestTelemetry.softwateFirmInfo.toUint32(); // Example firmware version
    TelemetryData.metrics[1].counter = latestTelemetry.deviceInfo.toUint32(); // Assuming 1 means OK
    TelemetryData.metrics[2].counter = 0; // Store temperature as counter
    TelemetryData.metrics[3].counter = 0; // Store temperature as counter1
    TelemetryData.metrics[4].counter = latestTelemetry.cameraInfo.toUint32(); // camera status,
    TelemetryData.metrics[5].counter = latestTelemetry.storageInfo.toUint32(); // storage status
    TelemetryData.metrics[6].counter = floor(epsVoltage * 1000); // Store EPS voltage as counter
    TelemetryData.metrics[7].counter = latestTelemetry.startTime.startTime; // Store start time in seconds since epoch
    // printCameraStatus(TelemetryData.metrics[4].counter);
    // printStorageStatus(TelemetryData.metrics[5].counter);
    // printDeviceStatus(TelemetryData.metrics[1].counter);
    // printSoftwareFirmStatus(TelemetryData.metrics[0].counter);
}