#include "GenericParam.h"
#include "XDLX_Kaya_UI.h"
#include "XDLX_Kaya_Grabber.h"
#include "XDLX_Camera.h"
#include "XDLX_Stream.h"
#include "XDLX_Telemetry.h"

std::atomic<bool> g_terminate(false);
std::condition_variable g_cv;
std::mutex g_mutex;
void transferLogOnExit();

int64_t TotalFrames= 2;
// bool CaptureCompleted= 0;
TelemetryInfo latestTelemetry;

int64_t RXFrameCounter[KY_MAX_CAMERAS];
int64_t DropFrameCounter[KY_MAX_CAMERAS];
int64_t RXPacketCounter[KY_MAX_CAMERAS];
int64_t DropPacketCounter[KY_MAX_CAMERAS];
double instantFps[KY_MAX_CAMERAS];
MetaSet MetaConfig;
// std::string userDirectory = "/home/xdlinx/KayaDevelopment/XDLX_KYFG_V6/";
std::string g_LogFilePath =  userDirectory + "Capture/KayaProcess.log";
ofstream ProcessLogFile;//(g_LogFilePath); // Create and open a text file
std::chrono::system_clock::time_point now;
long long presentTime;
long long previousTime;

// Signal handler
void handle_signal(int sig) {
    const char* signal_name = strsignal(sig);
    PRINT_LOG("[SYS]", "Signal received: " << signal_name << " (" << sig << "), cleaning up...\n");
    g_terminate = true;
    g_cv.notify_all(); // Wake up any waiting threads
#ifdef PRINT_FILE
    if(MSIAPP){
        transferLogOnExit();
    }
#endif
    // Restore default and re-raise to allow core dump if needed
    signal(sig, SIG_DFL);
    raise(sig);
}

// Call this to close and transfer log
void transferLogOnExit() {
    if (ProcessLogFile.is_open()) {
        ProcessLogFile.flush();
        ProcessLogFile.close();
    }
    Stream fileOperation;
    fileOperation.transferFile(g_LogFilePath);
}

// Register signal handlers at program start
void setup_signal_handlers() {
    signal(SIGINT, handle_signal);   // Ctrl+C
    signal(SIGTERM, handle_signal);  // kill
    signal(SIGSEGV, handle_signal);  // segfault
    signal(SIGBUS, handle_signal);   // bus error
    signal(SIGABRT, handle_signal);  // abort()
}

// A delay function
void WaitTillCompleted(float sec)
{
   
    std::this_thread::sleep_for(std::chrono::duration<float>(sec)); 
}

void getSystemUpdate(){
    PRINT_LOG("[I81]", "System Update Information\n");
    //get disk available space
    struct statvfs stat;
    if (statvfs("/", &stat) == 0) {
        unsigned long long free_space = stat.f_bsize * stat.f_bavail;
        double free_space_gb = static_cast<double>(free_space) / (1024 * 1024 * 1024);
        PRINT_LOG("[I82]", "Disk free space: " << std::fixed << std::setprecision(2) << free_space_gb << " GB" << std::endl);
        latestTelemetry.storageInfo.storageStatus = static_cast<uint32_t>(free_space_gb);
    } else {
        PRINT_LOG("[E44]", "Failed to get disk space info" << std::endl);
    }
    // Get current RAM Available (only in GB)
    double available_ram_gb = 0.0;
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.find("MemAvailable:") == 0) {
            std::istringstream iss(line);
            std::string key;
            long long value_kb;
            std::string unit;
            iss >> key >> value_kb >> unit;
            available_ram_gb = static_cast<double>(value_kb) / (1024 * 1024);
            break;
        }
    }
    if (available_ram_gb > 0.0) {
        PRINT_LOG("[I83]", "RAM available: " << std::fixed << std::setprecision(2) << available_ram_gb << " GB" << std::endl);
    } else {
        PRINT_LOG("[E45]", "Failed to get RAM info" << std::endl);
    }

    // Get CPU info
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string cpu_line;
    std::string model_name;
    int cpu_cores = 0;
    while (std::getline(cpuinfo, cpu_line)) {
        if (cpu_line.find("model name") != std::string::npos && model_name.empty()) {
            auto pos = cpu_line.find(":");
            if (pos != std::string::npos) {
                model_name = cpu_line.substr(pos + 2);
            }
        }
        if (cpu_line.find("processor") == 0) {
            cpu_cores++;
        }
    }
    if (!model_name.empty()) {
        PRINT_LOG("[I84]", "CPU Model: " << model_name << std::endl);
    }
    PRINT_LOG("[I85]", "CPU Cores: " << cpu_cores << std::endl);

    // Get system uptime
    std::ifstream uptime_file("/proc/uptime");
    double uptime_seconds = 0.0;
    if (uptime_file >> uptime_seconds) {
        int hours = static_cast<int>(uptime_seconds) / 3600;
        int minutes = (static_cast<int>(uptime_seconds) % 3600) / 60;
        int seconds = static_cast<int>(uptime_seconds) % 60;
        PRINT_LOG("[I86]", "Duration since System Up: " << hours << "h " << minutes << "m " << seconds << "s" << std::endl);

        // Calculate and print system boot time
        // auto now = std::chrono::system_clock::now();
        // std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        // std::time_t boot_time = now_c - static_cast<time_t>(uptime_seconds);
        // PRINT_LOG("[SYS] System Boot Time: " << std::put_time(std::localtime(&boot_time), "%Y-%m-%d %H:%M:%S") << std::endl);
    }

    // Get hostname
    // char hostname[256];
    // if (gethostname(hostname, sizeof(hostname)) == 0) {
    //     PRINT_LOG("[SYS] Hostname: " << hostname << std::endl);
    // }

    //Get Current PCIe Bandwidth
    // double AverageTime = 0.0;
    // double transferSpeed = -1;
    // while(true){
    //     KYFG_GrabberExecuteCommand(fgHandle, "BandwidthTestPerform");
    //     sleep(0.5);
    //     AverageTime = KYFG_GetGrabberValueFloat(fgHandle, "BandwidthTestAverageTime");
    //     transferSpeed = KYFG_GetGrabberValueFloat(fgHandle, "BandwidthTestAverageSpeed");
    //     if(transferSpeed != -1){
    //         break;
    //     }
    // }
    // PRINT_LOG("Average Transfer Speed: "<<transferSpeed<<"(MBps) "<<"Time: "<<AverageTime<<"(ns)"<<std::endl);


}

void CheckGrabberStatus(int interval) {
    std::unique_lock<std::mutex> lock(g_mutex);
    if (g_cv.wait_for(lock, std::chrono::seconds(interval), [] { return g_terminate.load(); })) {
        // Woke up early due to termination signal
        // PRINT_LOG("", "Grabber status thread interrupted due to termination." << std::endl);
        return;
    }

    if (latestTelemetry.cameraInfo.GrabberStatus == 1) {
        PRINT_LOG("", "Grabber is not connected after " << interval << "(sec) of grace time. Exiting from Capture Sequence." << std::endl);
        transferLogOnExit();
        MSIAPP = false;
        std::exit(0);
    } else if (latestTelemetry.cameraInfo.GrabberStatus == 2) {
        PRINT_LOG("", "Grabber connected successfully." << std::endl);
    }
}

// AutoConnect wrapper with timeout
int RunAutoConnectWithTimeout(Grabber& KayaGrabber, int timeoutSec) {
    auto fut = std::async(std::launch::async, [&]() {
        return KayaGrabber.AutoConnect();  // returns 0 or 1
    });

    if (fut.wait_for(std::chrono::seconds(timeoutSec)) == std::future_status::timeout) {
        PRINT_LOG("[E99]", "AutoConnect timed out after " << timeoutSec << " seconds" << std::endl);
        return -1;  // Timeout occurred
    }

    return fut.get();  // Either 0 (fail) or 1 (success)
}

int startMSIApp(int argc, char *argv[])
{
    MSIAPP = true;
    requestLogTimerReset();
    ProcessLogFile.open(g_LogFilePath);
    if(!ProcessLogFile.is_open()){
        std::cerr << "Failed to open log file: " << g_LogFilePath << std::endl;
    }
    setup_signal_handlers(); // Register signal handlers
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    latestTelemetry.startTime.startTime = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
    PRINT_LOG("[I01]", "Program started at: " << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << std::endl);
    latestTelemetry.cameraInfo.GrabberStatus = 1; //Grabber Status 1: Not Connected, 2: Connected
    std::thread GrabberThread(CheckGrabberStatus, 10);
    // previousTime = presentTime;
    getSystemUpdate();
    UI KayaUI;//("ConfigData/param123.txt");
    std::string GrabberName= "KAYA Dev Device 0xD001";
    Grabber KayaGrabber;
    int grabberindex=0;
    Camera Vislink;
    Stream VisStream[KY_MAX_CAMERAS];
    Stream fileOperation;
    KYFGLib_InitParameters kyInit;
    Telemetry telemetry;

    if (FGSTATUS_OK != KYFGLib_Initialize(&kyInit))//Initialize the KYFGLib_InitParameters Structure
    {
        PRINT_LOG("[E01]", "Library initialization failed"<<endl);
#ifdef PRINT_FILE
        transferLogOnExit();
#endif
        MSIAPP = false;
        if (GrabberThread.joinable()){
            g_terminate = true;
            g_cv.notify_all(); // Just in case no signal was sent
            GrabberThread.join(); // Wait for the Grabber status thread to finish
        }
        return 1;
    }

    bool timeStatus = KayaUI.readInput(argc, argv);
    if(!timeStatus)
    {
        PRINT_LOG("[E02]", "Invalid Time Difference. Capture Time has elapsed. Exiting..."<<endl);
#ifdef PRINT_FILE
    transferLogOnExit();
#endif
        MSIAPP = false;
        if (GrabberThread.joinable()){
            g_terminate = true;
            g_cv.notify_all(); // Just in case no signal was sent
            GrabberThread.join(); // Wait for the Grabber status thread to finish
        }
        return 1;
    }
    // std::this_thread::sleep_for(std::chrono::milliseconds(50));
    KayaGrabber.DisplayList();
    bool validGrabber= KayaGrabber.AutoConnect();
    // int validGrabber = RunAutoConnectWithTimeout(KayaGrabber, 10);

    for(int CamID=0; CamID<KY_MAX_CAMERAS; CamID++)
    {
        VisStream[CamID].DeleteStream(Vislink.Handle(CamID), CamID);
    }
    if(validGrabber == 1)
    {
        int CamID= 0;
        FGHANDLE fgHandle= KayaGrabber.GetHandle();
        // getSystemUpdate(fgHandle);
        // sleep(2);
        KayaGrabber.getHardwareInfo(fgHandle);
        Vislink.Detect(fgHandle);//Scan for the available Camera for this grabber
        Vislink.ConnectDesired(fgHandle, CamID);//Connect Specific/all the available Camera for this grabber
        CAMHANDLE CamHandle = Vislink.Handle(0);
        bool pollStatus = telemetry.initialize(fgHandle, CamHandle);
        if(pollStatus){
            PRINT_LOG("", "Telemetry initialized successfully." << std::endl);
        }else{
            PRINT_LOG("", "Telemetry initialization failed." << std::endl);
        }
        commonParams.CoreTemperature = KYFG_GetGrabberValueInt(fgHandle, "DeviceTemperature");
        PRINT_LOG("[I87]", "Device Core Temperature: "<<commonParams.CoreTemperature<<endl);
        FGSTATUS Temp = KYFG_SetCameraValueEnum(CamHandle, "DeviceTemperatureSelector", 0);
        if(Temp!=FGSTATUS_OK){
            PRINT_LOG("[E21]", "Failed to set API for SensorTemp"<<endl);
        }
        commonParams.SensorTemperature = KYFG_GetCameraValueFloat(CamHandle, "DeviceTemperature");
        PRINT_LOG("[I54]", "SensorTemp: "<<commonParams.SensorTemperature<<endl);

        Vislink.SettingsDefault(fgHandle, CamHandle);//Apply appropriate Settings for Specific/all the available cameras
        KayaUI.ProcessInput(argc, argv, MetaConfig);
        Vislink.ConnectDesiredRegion(CamHandle, G_Band);
        Vislink.SettingsJSON(fgHandle, CamHandle);//Apply appropriate Settings for Specific/all the available cameras
        Vislink.SettingUserInput(fgHandle, CamHandle);    
        Vislink.getSettingsApplied(fgHandle, CamHandle);
        if(G_TDI_Modes!=1){
            TotalFrames= floor(G_FPS*G_Duration);//Find the FPS and duration and compute total frames
            
            PRINT_LOG("[I55]", "FPS: "<<G_FPS<<"  Duration: "<<G_Duration<<" = Total no of frames: "<<TotalFrames<<std::endl); 
            if(TotalFrames<1){
                PRINT_LOG("[E03]", "Invalid Total Frames: "<<TotalFrames<<". It should be greater than 0.\n");
                if(Vislink.IsConnected(CamID)!= KYFALSE)
                    VisStream[CamID].DeleteStreamMap(Vislink.Handle(CamID), CamID);
                Vislink.CloseAll();     //Close all the Cameras
                KayaGrabber.Close();
#ifdef PRINT_FILE
                transferLogOnExit();
#endif
                MSIAPP = false;
                if (GrabberThread.joinable()){
                    g_terminate = true;
                    g_cv.notify_all(); // Just in case no signal was sent
                    GrabberThread.join(); // Wait for the Grabber status thread to finish
                }
                return 1;
            }
        }
               

        FGSTATUS status =  KYFG_SetCameraValue(CamHandle, "ChunkSatelliteBus", (void*)&MetaConfig);//Update Metadata info

        if(Vislink.IsConnected(CamID)!= KYFALSE)
            VisStream[CamID].CreateStreamMap(Vislink.Handle(CamID), CamID);
        
        PRINT_LOG("[I56]", "Wait for time to trigger...\n");//wait for the time to trigger
        int64_t msec= TimeDifferenceUpdated(&G_Time, 1);
        if(msec<0) msec=0;
        latestTelemetry.cameraInfo.cameraStatus = 2; // Camera is waiting to capture
        std::this_thread::sleep_for(std::chrono::milliseconds(msec));
        
        PRINT_LOG("[I57]", "Waiting Time Completed\n");

        // auto end = std::chrono::high_resolution_clock::now();
        
        for(int CamID=0; CamID<KY_MAX_CAMERAS; CamID++) //Start the Required cameras
        {
            if(Vislink.IsConnected(CamID)!= KYFALSE)
                Vislink.StartCamera(KayaGrabber.GetID(), CamID, VisStream[CamID].GetCamstreamHandle(), TotalFrames);                 
        }
        std::thread th1(WaitTillCompleted, G_Duration+1);// Wait for the duration to complete the Cam operation + 1Sec
        th1.join();

        for(int CamID=0; CamID<KY_MAX_CAMERAS; CamID++)//Delete all the open stream
        {
            if(Vislink.IsConnected(CamID)!= KYFALSE)
                VisStream[CamID].DeleteStreamMap(Vislink.Handle(CamID), CamID);
        }
        storeData = false; // Reset the storeData flag after processing
        commonParams.CoreTemperature = KYFG_GetGrabberValueInt(fgHandle, "DeviceTemperature");
        PRINT_LOG("[I87]", "Device Core Temperature: "<<commonParams.CoreTemperature<<endl);
        Temp = KYFG_SetCameraValueEnum(CamHandle, "DeviceTemperatureSelector", 0);
        if(Temp!=FGSTATUS_OK){
            PRINT_LOG("[E21]", "Failed to set API for SensorTemp"<<endl);
        }
        commonParams.SensorTemperature = KYFG_GetCameraValueFloat(CamHandle, "DeviceTemperature");
        PRINT_LOG("[I54]", "SensorTemp: "<<commonParams.SensorTemperature<<endl);
        VisStream[CamID].freeBuffer();
        Stream config;

#ifndef BINNING
    bool writeStatus = VisStream[CamID].writeData();
    PRINT_LOG("[I88]", (writeStatus ? "Data written successfully\n" : "Data write unsuccessful\n"));
#else
    auto binStart = std::chrono::high_resolution_clock::now();
    bool binStatus = VisStream[CamID].ProcessData();
    PRINT_LOG("[I89]", (binStatus ? " Data Processing completed successfully\n" : "Data Processing unsuccessful\n"));
    auto binEnd = std::chrono::high_resolution_clock::now();
    double binTime = std::chrono::duration<double>(binEnd - binStart).count();
    PRINT_LOG("[I90]", "Total time took for data processing: " << binTime << " seconds\n");
#endif
        config.saveConfigInfo(Vislink.Handle(CamID));//Handle all the parameter to jSON file for next process
        telemetry.terminate();
    }
    
    Vislink.CloseAll();     //Close all the Cameras
    KayaGrabber.Close();    //Close the Grabber
    // fileOperation.saveCommonConfig();
    
    if (GrabberThread.joinable()){
        g_terminate = true;
        g_cv.notify_all(); // Just in case no signal was sent
        GrabberThread.join(); // Wait for the Grabber status thread to finish
    }

    PRINT_LOG("[I91]", "Exiting..."<<std::endl);
#ifdef PRINT_FILE
    transferLogOnExit();
#endif
    MSIAPP = false;
    return 0;
}
