#include "GenericParam.h"

using namespace std;

std::string Bandfile;
std::string folder;
std::mutex file_mutexes[7];
size_t ephIndex = 0;           // keep track of where we are
size_t ephWriteCount = 5;      // whatever number you want to write per frame
int sizeofEphData = sizeof(AdcsEphemerisData); // size of each ephemeris data structure

FileParam Param;
// char acquisitionPath[Max_Curr_Path_Len] = "Capture/";
std::string acquisitionPath = userDirectory + "Capture/";
char ConfFileName[Max_File_Path_Len];
uint32_t metadataIndex = 0;

// #define FGLIB_ALLOCATED_BUFFERS   //Kaya allocated buffers
#ifdef __linux__ // _aligned_malloc() implementation for __linux__
#include <signal.h>
void *_aligned_malloc(size_t size, size_t alignment)
{
    size_t pageAlign = size % 4096;
    if (pageAlign)
    {
        size += 4096 - pageAlign;
    }

#if (GCC_VERSION <= 40407)
    void *memptr = 0;
    posix_memalign(&memptr, alignment, size);
    return memptr;
#else
    return aligned_alloc(alignment, size);
#endif
}
#define _aligned_free free
#endif // #ifdef __linux__

// Create Stream for Single Band
class Stream
{
private:
    STREAM_HANDLE cameraStreamHandle; // there are maximum KY_MAX_CAMERAS cameras
    size_t frameDataSize, frameDataAligment;
    STREAM_BUFFER_HANDLE streamBufferHandle[MAX_STREAM_BUFFER] = {0};
    // STREAM_BUFFER_HANDLE streamBufferHandle[maxBufferStream] = { 0 };
    void *pBuffer[_countof(streamBufferHandle)] = {NULL};
    int CamId;

public:
    // Stream();
    int CreateStreamMap(CAMHANDLE camHandle, int CamID);
    STREAM_HANDLE GetCamstreamHandle();
    void DeleteStream(CAMHANDLE camHandle, int CamID);
    void DeleteStreamMap(CAMHANDLE camHandle, int CamID);
    void saveConfigInfo(CAMHANDLE camHandle);
    void transferFile( const std::string& logPath );
    void saveCommonConfig();
    void freeBuffer();
    bool ProcessData();
    bool writeData();
    // bool StopAll();
};

int64_t CaptureImageMap(char *pFrameMemory, size_t bufferSize)
{
    int64_t nBytes = 0;

    static double lastTimeDifference = 0;
    static bool firstFrame = true;

    if (Param.offset + bufferSize <= Param.file_size)
    {
        uint8_t *pMetaData = (uint8_t *)pFrameMemory;
        pMetaData = pMetaData + Param.MetaStartPos;
        memset(&Param.metadataChunks, 0, sizeof(METADATA_CHUNKS));
        memcpy(&Param.metadataChunks, pMetaData, sizeof(METADATA_CHUNKS));
        Param.timestamp = Param.metadataChunks.TimeCounter;

#ifdef METAFILE
        fprintf(Param.fpMeta, "\n%11ld,    %.4s,   %.4s,    %d,  %d,   %d, %13d, %7d,   %f,    %f, %9d, %10ld,    %ld,       %f", 
            Param.CaptureCount,
            Param.metadataChunks.XDLX, 
            Param.metadataChunks.SAT_ID, 
            Param.metadataChunks.OrbitNumber, 
            Param.metadataChunks.Task_ID, 
            Param.metadataChunks.ImageStartTime, 
            Param.metadataChunks.ImagingDuration, 
            Param.metadataChunks.ConfigAndTDIFile, 
            Param.metadataChunks.Latitude, 
            Param.metadataChunks.Longitude, 
            Param.metadataChunks.PPSRef, 
            Param.metadataChunks.TimeRef, 
            Param.metadataChunks.TimeCounter,
            Param.TimeDifference); 
#endif
        if (Param.lastTimestamp > 0 && Param.timestamp != Param.lastTimestamp)
            Param.TimeDifference = (Param.timestamp - Param.lastTimestamp) * 0.000008;

        if (!firstFrame && lastTimeDifference > 0)
        {
            double expectedFrameTime = lastTimeDifference;
            double ratio = Param.TimeDifference / expectedFrameTime;

            if (ratio > 1.2)  // still use the threshold
            {
                int droppedFrames = static_cast<int>(ratio) - 1;

                PRINT_LOG("", " TimeDifference: " << Param.TimeDifference
                        << " framedrops detected! (~" << droppedFrames
                        << ", Prev: " << lastTimeDifference << ")" << endl);

                Param.framedrops += droppedFrames;
            }
            else
            {
                PRINT_LOG("", " TimeDifference: " << Param.TimeDifference << endl);
            }
        }
        else
        {
            PRINT_LOG("", " TimeDifference: " << Param.TimeDifference << endl);
            firstFrame = false;
        }
        printf("PPSRef: %u  ", Param.metadataChunks.PPSRef);
        printf("TimeRef: %ld  ", Param.metadataChunks.TimeRef);
        printf("TimeCounter: %ld ", Param.metadataChunks.TimeCounter);
        std::cout<<"TimeDifference: " << Param.TimeDifference<<" ";
        printf("Lat: %f ",Param.metadataChunks.Latitude);
        printf("Lon: %f\n",Param.metadataChunks.Longitude);

        lastTimeDifference = Param.TimeDifference;

        memcpy(Param.file_memory + Param.offset, pFrameMemory, bufferSize);
        memset(pFrameMemory, 0, bufferSize);
        Param.CaptureCount++;
        Param.offset += bufferSize;
        Param.lastTimestamp = Param.timestamp;
    }else
    {
        PRINT_LOG("[E24]", "Buffer overflow detected! Offset: " << Param.offset << ", Buffer Size: " << bufferSize << ", File Size: " << Param.file_size << endl);
        return -1; // Buffer overflow
    }
    return nBytes;
}

void printAdcsEphemerisData()
{
    const auto &adcs = latest_gnss_eph_data.adcs_eph_data;
    std::cout << "=== ADCS Ephemeris Data ===\n";
    std::cout << "Orbit Time: " << adcs.orbit_time << " s\n";

    std::cout << "ECI Position (km): "
              << adcs.eci_position_x << ", "
              << adcs.eci_position_y << ", "
              << adcs.eci_position_z << "\n";

    std::cout << "ECI Velocity (km/s): "
              << adcs.eci_velocity_x << ", "
              << adcs.eci_velocity_y << ", "
              << adcs.eci_velocity_z << "\n";

    std::cout << "ECEF Position (km): "
              << adcs.ecef_position_x << ", "
              << adcs.ecef_position_y << ", "
              << adcs.ecef_position_z << "\n";

    std::cout << "ECEF Velocity (km/s): "
              << adcs.ecef_velocity_x << ", "
              << adcs.ecef_velocity_y << ", "
              << adcs.ecef_velocity_z << "\n";

    std::cout << "Angular Rate (deg/s): "
              << adcs.ang_rate_x << ", "
              << adcs.ang_rate_y << ", "
              << adcs.ang_rate_z << "\n";

    std::cout << "Attitude Quaternion: "
             << adcs.att_quat_1 << ", "
             << adcs.att_quat_2 << ", "
             << adcs.att_quat_3 << ", "
             << adcs.att_quat_4 << "\n";
    std::cout << "EPS Voltage: "
              << epsVoltage<<"\n";
    std::cout << "Latitude: " << adcs.latitude << "\n";
    std::cout << "Longitude: " << adcs.longitude << "\n";
    std::cout << "Altitude: " << adcs.altitude << " km\n";
    std::cout << "Nadir Vector: " << adcs.nadir_vector_x << ", " << adcs.nadir_vector_y << ", " << adcs.nadir_vector_z << "\n";
    std::cout << "Geodetic Nadir Vector: " << adcs.gd_nadir_vector_x << ", " << adcs.gd_nadir_vector_y << ", " << adcs.gd_nadir_vector_z << "\n";
    std::cout << "Beta Angle: " << adcs.beta_angle << "\n";

    const char* flags[] = {
        "Time Validity", "ECI Pos Valid", "ECI Vel Valid", "ECEF Pos Valid",
        "ECEF Vel Valid", "Rate Valid", "Attitude Valid",
        "Lat-Lon-Alt Valid", "Nadir Vector Valid", "Geo Nadir Valid", "Beta Angle Valid"
    };

    std::cout << "Validity Flags:\n";
    for (int i = 0; i < static_cast<int>(sizeof(flags)/sizeof(flags[0])); ++i) {
        int bit = (adcs.validity_flags >> i) & 1;
        std::cout << "  " << flags[i] << ": " << bit << "\n";
    }
}



void Stream_callback_func(STREAM_BUFFER_HANDLE streamBufferHandle, void *userContext)
{
    char *pFrameMemory = 0;
    uint32_t frameId = 0;
    static uint64_t iFrames = 0;
    size_t bufferSize = 0;
#ifndef MINIMAL_CALLBACK
    uint64_t timeStamp;
    double instantFps;
#endif
    userContext; // Suppress warning

    if (NULL_STREAM_BUFFER_HANDLE == streamBufferHandle)
    {
        return; // This callback indicates that acquisition has stopped
    }
    // printAdcsEphemerisData();
    // As a minimum, application needs to get pointer to current frame memory
    KYFG_BufferGetInfo(streamBufferHandle, KY_STREAM_BUFFER_INFO_BASE, &pFrameMemory, NULL, NULL);
    KYFG_BufferGetInfo(streamBufferHandle, KY_STREAM_BUFFER_INFO_SIZE, &bufferSize, NULL, NULL);

    // Additionaly the following information can be obtained:
#ifndef MINIMAL_CALLBACK
    KYFG_BufferGetInfo(streamBufferHandle, KY_STREAM_BUFFER_INFO_ID, &frameId, NULL, NULL);
    // KYFG_BufferGetInfo(streamBufferHandle, KY_STREAM_BUFFER_INFO_USER_PTR, &pUserContext, NULL, NULL);
    KYFG_BufferGetInfo(streamBufferHandle, KY_STREAM_BUFFER_INFO_TIMESTAMP, &timeStamp, NULL, NULL);
    KYFG_BufferGetInfo(streamBufferHandle, KY_STREAM_BUFFER_INFO_INSTANTFPS, &instantFps, NULL, NULL);

#endif
    PRINT_LOG("", "FrameNo=" << std::setw(4) << std::setfill(' ') << iFrames++ << "(BufNo=" << std::setw(2) << std::setfill('0') << frameId << "), timeStamp= " << timeStamp << ", instantFps=" << instantFps<<", BufferSize=" << bufferSize);

    int64_t nBytes = CaptureImageMap(pFrameMemory, bufferSize);
    KYFG_BufferToQueue(streamBufferHandle, KY_ACQ_QUEUE_INPUT);
}

STREAM_HANDLE Stream::GetCamstreamHandle()
{
    if (this->cameraStreamHandle != INVALID_STREAMHANDLE)
        return (this->cameraStreamHandle);
    else
        return (-1);
}

size_t getAvailableRAMBytes()
{
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    size_t available_kb = 0;

    while (std::getline(meminfo, line))
    {
        if (line.find("MemAvailable:") == 0)
        {
            sscanf(line.c_str(), "MemAvailable: %lu kB", &available_kb);
            break;
        }
    }

    return available_kb * 1024; // convert to bytes
}

int Stream::CreateStreamMap(CAMHANDLE camHandle, int CamID)
{
    Param.CamHandle = camHandle;
    CamID = 0;
    int CameraID = 0;

    char CurrentFolder[Max_File_Path_Len];
    char MetaFileName[Max_File_Path_Len];

    PRINT_LOG("[I58]",  "Stream Created... \n");
    this->CamId = CamID;
    if (FGSTATUS_OK == KYFG_CameraOpen2(camHandle, NULL))
    {
        Param.offset = 0;
        Param.CaptureCount = 0;
        Param.framedrops = 0;

        int Width = KYFG_GetCameraValueInt(camHandle, "Width");
        int Height = KYFG_GetCameraValueInt(camHandle, "Height");
        Param.MetaStartPos = (Width * 10 * (Height - 1)) / 8;
        sprintf(CurrentFolder, "%sAcq%06d_%02d%02d%02d%02d%02d%04d/", acquisitionPath.c_str(), G_Task, G_Time.tm_sec, G_Time.tm_min, G_Time.tm_hour, G_Time.tm_mday, G_Time.tm_mon, G_Time.tm_year);
        if (mkdir(CurrentFolder, 0777) == -1)
        {
            if (errno == EEXIST)
            {
                PRINT_LOG("[I59]", "Directory already exists: " << CurrentFolder << endl);
            }
            else
            {
                PRINT_LOG("[E23]", "Error creating directory " << CurrentFolder << ": " << strerror(errno) << endl);
            }
        }
        else
        {

            PRINT_LOG("[I60]", "Directory created: " << CurrentFolder << endl);
        }
        folder = CurrentFolder;
        sprintf(ConfFileName, "%sAcq%06d_%02d%02d%02d%02d%02d%04d.json", folder.c_str(), G_Task, G_Time.tm_sec, G_Time.tm_min, G_Time.tm_hour, G_Time.tm_mday, G_Time.tm_mon, G_Time.tm_year); // Default Image Mode
        if (FGSTATUS_OK != KYFG_StreamCreate(camHandle, &(this->cameraStreamHandle), 0))
        {

            PRINT_LOG("[E25]", "Error: KYFG_StreamCreate" << std::endl);
        } // Create stream
        if (FGSTATUS_OK != KYFG_StreamBufferCallbackRegister(this->cameraStreamHandle, Stream_callback_func, NULL))
        {
            PRINT_LOG("[E26]", "Error: KYFG_StreamBufferCallbackRegister" << std::endl);
        }
        if (FGSTATUS_OK != KYFG_StreamGetInfo(this->cameraStreamHandle, KY_STREAM_INFO_PAYLOAD_SIZE, &frameDataSize, NULL, NULL))
        {
            PRINT_LOG("[E27]", "Error: KYFG_StreamGetInfo, KY_STREAM_INFO_PAYLOAD_SIZE" << std::endl);
        } // Retrieve information about required frame buffer size and alignment
        if (FGSTATUS_OK != KYFG_StreamGetInfo(this->cameraStreamHandle, KY_STREAM_INFO_BUF_ALIGNMENT, &frameDataAligment, NULL, NULL))
        {
            PRINT_LOG("[E28]", "Error: KYFG_StreamGetInfo, KY_STREAM_INFO_BUF_ALIGNMENT" << std::endl);
        }

        // Allocate memory for desired number of frame buffers
        for (int iFrame = 0; iFrame < _countof(streamBufferHandle); iFrame++)
        {
#ifdef FGLIB_ALLOCATED_BUFFERS
#pragma message("Building with KYFGLib allocated buffers")
            KYFG_BufferAllocAndAnnounce(this->cameraStreamHandle, frameDataSize, NULL, &streamBufferHandle[iFrame]);
#else
#pragma message("[I63] Building with user allocated buffers")
            pBuffer[iFrame] = _aligned_malloc(frameDataSize, frameDataAligment);
            // PRINT_LOG("Allocated framesDataSize: "<<frameDataSize<<" At frameDataAlignment: "<<frameDataAligment<<std::endl);
            if (FGSTATUS_OK != KYFG_BufferAnnounce(this->cameraStreamHandle, pBuffer[iFrame], frameDataSize, NULL, &streamBufferHandle[iFrame]))
            {

                PRINT_LOG("[E30]", "Error: KYFG_BufferAnnounce " << std::endl);
            }
#endif
        }
        size_t availableRAM = getAvailableRAMBytes();

        PRINT_LOG("[I64]", "Total Ram Available: " << availableRAM / (1024.0 * 1024.0) << " (MB)" << endl);
        // Optional: Reserve only 80% of available RAM
        availableRAM = (availableRAM * 80) / 100;
        PRINT_LOG("[I65]", "Total Ram can Allocate: " << availableRAM / (1024.0 * 1024.0) << " (MB)" << endl);
        int maxFrames = availableRAM / frameDataSize;
        if (maxFrames < 1)
        {
            PRINT_LOG("[E31]", "ERROR: Not enough RAM to allocate even 1 frame buffer!" << endl);
            return -1;
        }
        if (maxFrames < TotalFrames)
        {
            int TempFrames = TotalFrames;
            TotalFrames = maxFrames;
            double TempDuration = G_Duration;
            G_Duration = TotalFrames/G_FPS;
            PRINT_LOG("[I66]", "NOTE: Reduced TotalFrames from " << TempFrames << " to " << TotalFrames << " and duration from "<<TempDuration<<" to "<<G_Duration <<" due to RAM limits." << endl);

        }
        Param.file_size = frameDataSize * TotalFrames; // Initializing file size to $kb.
        latestTelemetry.deviceInfo.Duration = G_Duration*1000; // Capture duration in milliseconds

        PRINT_LOG("[I67]", "File_size=" << Param.file_size << "(FrameSize=" << frameDataSize << " TotalFrames=" << TotalFrames << ")" << endl);
        Param.file_memory = (char *)malloc(Param.file_size);
        PRINT_LOG("", "Allocated Ram: " << Param.file_size / (1024.0 * 1024.0) << " (MB)" << endl);
        // }

#ifdef METAFILE
        sprintf(MetaFileName, "%sAcq%06d_%02d%02d%02d%02d%02d%04d.txt", folder.c_str(), G_Task, G_Time.tm_sec, G_Time.tm_min, G_Time.tm_hour, G_Time.tm_mday, G_Time.tm_mon, G_Time.tm_year); // Default Image Mode
        Param.fpMeta= fopen(MetaFileName, "wb");
        if(Param.fpMeta != NULL)	
        {
            PRINT_LOG ("", "Meta File " << MetaFileName << " opened... \n");
            fprintf(Param.fpMeta, "CaptureCount   MfrID   SatID   OrbitNo      TaskID   ImgStart    ImgDuration   ConfID     Latitude     Longitude     PPSRef     TimeRef       TimeCounter FrameConsistency");
        }else{
            PRINT_LOG ("", "Meta File " << MetaFileName << " open failed... \n");
        }
#endif
    }
    else
    {
        PRINT_LOG("[E32]", "Unsuccessful Camera not opened");
        return (-1);
    }
    return CamID;
}

void Stream::DeleteStream(CAMHANDLE camHandle, int CamID)
{
    if (camHandle != INVALID_CAMHANDLE)
    {

        if (FGSTATUS_OK == KYFG_CameraStop(camHandle))
            ;// PRINT_LOG("[I08] Camera successfully stoped\n");
        else
            ;// PRINT_LOG("[E05] Camera stop unsuccessful\n");
    }
    if (this->cameraStreamHandle != INVALID_STREAMHANDLE)
    {

        if (FGSTATUS_OK == KYFG_StreamDelete(this->cameraStreamHandle))
            ;// PRINT_LOG("[I09] Camera Stream successfully deleted\n");
        else
            ;// PRINT_LOG("[E06] Stream delete unsuccessful\n");
    }
}

void Stream::DeleteStreamMap(CAMHANDLE camHandle, int CamID)
{

    if (camHandle != INVALID_CAMHANDLE)
    {
        if (FGSTATUS_OK == KYFG_CameraStop(camHandle))
        {

            PRINT_LOG("[I68]", "Camera successfully stoped...\n");
        }
        else
        {
            PRINT_LOG("[E35]", "Failed to stop Camera\n");
        }
    }

    if (this->cameraStreamHandle != INVALID_STREAMHANDLE)
    {

        if (FGSTATUS_OK == KYFG_StreamDelete(this->cameraStreamHandle))
        {
            PRINT_LOG("[I69]", "Stream successfully deleted...\n");
        }
        else
        {
            PRINT_LOG("[E36]", "Failed to delete Stream.\n");
        }
    }

    // if(Param.fd != -1){

    PRINT_LOG("[I70]", "TotalNoOfFrames: " << TotalFrames << " CapturedCount: " << Param.CaptureCount << endl);
    if (TotalFrames != Param.CaptureCount)
    {
        Param.file_size = frameDataSize * Param.CaptureCount;
        // ftruncate(Param.fd, Param.file_size);

        PRINT_LOG("[E37]", "Error: Less data aquired with file size: " << Param.file_size << endl);
    }
    PRINT_LOG("", "No.of Framedrops: " << Param.framedrops << endl);

#ifdef METAFILE
    if(Param.fpMeta != NULL){
        fflush(Param.fpMeta);
        fclose(Param.fpMeta);
        PRINT_LOG("", " Meta File flushed and closed...");
    }
#endif
}

void Stream::freeBuffer()
{
#ifndef FGLIB_ALLOCATED_BUFFERS
    for (int iFrame = 0; iFrame < _countof(streamBufferHandle); iFrame++)
    {
        if (pBuffer[iFrame])
        {
            _aligned_free(pBuffer[iFrame]);
            pBuffer[iFrame] = nullptr;
        }
    }

    PRINT_LOG("[I73]", "Allocated Buffers free done." << endl);
#endif
}
void Stream::saveConfigInfo(CAMHANDLE camHandle)
{

    json config;
    config["Height"] = commonParams.Height;
    config["Width"] = commonParams.Width;
    config["TDIMode"] = commonParams.TDIMode;
    config["TDIStages"] = commonParams.TDIStages;
    config["RegionHeight"] = commonParams.regionHeight;
    config["CapturesFolder"] = folder;
    config["ProcMode"] = ArguementsProcessed;
    config["Binning"] = G_Binning;
    config["BandHeight"] = commonParams.bandHeight;
    config["TotalFrames"] = commonParams.totalFrames;
    config["FPS"] = commonParams.FPS;
    config["ExposureTime"] = commonParams.ExpTime;
    config["Gain"] = commonParams.Gain;
    config["BandXShift"] = commonParams.BandXShift;
    config["CCSDSProcessStatus"] = G_CCSDSProcessStatus;
    config["ephWriteCount"] = ephWriteCount;
    config["sizeofEphData"] = sizeofEphData+4; // +4 for the size and index of the data
    config["coreTemperature"] = commonParams.CoreTemperature;
    config["sensorTemperature"] = (int16_t)floor(commonParams.SensorTemperature*100);
    // config["EPSVoltage"] = epsVoltage;
    std::string jsonPath = ConfFileName;
    std::string dirPath = jsonPath.substr(0, jsonPath.find_last_of("/\\"));
    if (!dirPath.empty() && !std::filesystem::exists(dirPath))
    {
        std::filesystem::create_directories(dirPath); // Create the directory if it doesn't exist
    }
    std::ofstream outFile(jsonPath);
    if (!outFile)
    {
        PRINT_LOG("[E40]", "Failed to open JSN file for writing configurations of the session: " << jsonPath << std::endl);
        return;
    }
    outFile << config.dump(4);
    outFile.close();

    PRINT_LOG("[I74]", "Configuration saved to JSON successfully." << std::endl);
}

bool Stream::writeData()
{
    std::string folderTrimmed = folder;
    if (!folderTrimmed.empty() && folderTrimmed.back() == '/')
        folderTrimmed.pop_back();

    std::filesystem::path folderPath(folderTrimmed);
    std::string folderName = folderPath.filename().string();
    std::filesystem::path rawFile = folderPath / (folderName + ".raw");

    size_t totalSize = Param.CaptureCount * frameDataSize;
    char *writePtr = Param.file_memory;

    // Open file using low-level POSIX API
    int fd = open(rawFile.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd == -1)
    {
        PRINT_LOG("[E38]", "Failed to open file for writing");
        free(Param.file_memory);
        Param.file_memory = nullptr;
        return false;
    }

    size_t writtenBytes = 0;
    while (writtenBytes < totalSize)
    {
        ssize_t result = write(fd, writePtr + writtenBytes, totalSize - writtenBytes);
        if (result == -1)
        {
            PRINT_LOG("[E39]", "write failed");
            break;
        }
        writtenBytes += result;
    }

    close(fd);

    if (writtenBytes == totalSize)
    {
        PRINT_LOG ( "[I71]", "Successfully wrote all " << writtenBytes << " bytes to file.\n");
    }
    else
    {
        PRINT_LOG ( "[E40]", "Partial write: " << writtenBytes << " of " << totalSize << " bytes.\n");
    }

    free(Param.file_memory);
    Param.file_memory = nullptr;
    return (writtenBytes == totalSize);
}

inline void unpack10BitLE(const uint8_t *src, uint16_t *dst, size_t pixelCount)
{
    size_t blocks = pixelCount / 4;
    for (size_t i = 0, j = 0; i < blocks; ++i, src += 5, j += 4)
    {
        dst[j + 0] = (src[1] & 0x03) << 8 | src[0];
        dst[j + 1] = (src[2] & 0x0F) << 6 | (src[1] >> 2);
        dst[j + 2] = (src[3] & 0x3F) << 4 | (src[2] >> 4);
        dst[j + 3] = src[4] << 2 | (src[3] >> 6);
    }
}

inline void pack10BitLE(const uint16_t *src, uint8_t *dst, size_t pixelCount)
{
    size_t blocks = pixelCount / 4;
    for (size_t i = 0, j = 0; i < blocks; ++i, src += 4, j += 5)
    {
        dst[j + 0] = src[0] & 0xFF;
        dst[j + 1] = ((src[0] >> 8) & 0x03) | ((src[1] & 0x3F) << 2);
        dst[j + 2] = ((src[1] >> 6) & 0x0F) | ((src[2] & 0x0F) << 4);
        dst[j + 3] = ((src[2] >> 4) & 0x3F) | ((src[3] & 0x03) << 6);
        dst[j + 4] = (src[3] >> 2) & 0xFF;
    }
}

inline void binning2x2(const uint16_t *in, uint16_t *out, int width, int height)
{
    int outW = width / 2;
    int outH = height / 2;
    for (int i = 0; i < outH; ++i)
    {
        const uint16_t *row0 = in + (2 * i) * width;
        const uint16_t *row1 = row0 + width;
        uint16_t *outRow = out + i * outW;
        for (int j = 0; j < outW; ++j)
        {
            int idx = 2 * j;
            outRow[j] = static_cast<uint16_t>(
                (row0[idx] + row0[idx + 1] + row1[idx] + row1[idx + 1]) / 4);
        }
    }
}

void process_band_bin(const uint8_t *frameBuffer, size_t bufferSize,
                      int bandId, int frameNo, int activeIndex,
                      std::ofstream &fout)
{
    try
    {

        if (!fout.is_open())
        {
            PRINT_LOG( "[E]", "Output file is not open for band " << bandId << "\n");
            return;
        }

        size_t pixels = static_cast<size_t>(commonParams.Width) * commonParams.bandHeight;
        size_t packedSize = (pixels * 10) / 8;
        size_t binnedPixels = (commonParams.Width / 2) * (commonParams.bandHeight / 2);
        size_t packedOutSize = (binnedPixels * 10) / 8;

        uint64_t offset = static_cast<uint64_t>(commonParams.Width) * commonParams.Height * frameNo;
        uint64_t bandOffset = static_cast<uint64_t>(commonParams.Width) * commonParams.bandHeight * activeIndex;
        uint64_t bandStart = (offset + bandOffset) * 10 / 8;

        size_t requiredEnd = bandStart + packedSize;
        if (requiredEnd > bufferSize)
        {
            PRINT_LOG( "[E]", "Band " << bandId << " frame " << frameNo
                      << " out-of-bounds read! Start: " << bandStart
                      << ", Required: " << requiredEnd
                      << ", Buffer size: " << bufferSize << "\n");
            return;
        }

        const uint8_t *bandData = frameBuffer + bandStart;

        std::vector<uint16_t> unpacked(pixels);
        std::vector<uint16_t> binned(binnedPixels);
        std::vector<uint8_t> packed(packedOutSize);

        unpack10BitLE(bandData, unpacked.data(), pixels);
        binning2x2(unpacked.data(), binned.data(), commonParams.Width, commonParams.bandHeight);
        pack10BitLE(binned.data(), packed.data(), binnedPixels);

        {
            std::lock_guard<std::mutex> lock(file_mutexes[bandId]);
            fout.write(reinterpret_cast<const char *>(packed.data()), packedOutSize);
        }
    }
    catch (const std::exception &e)
    {
        PRINT_LOG( "[E]", "Band " << bandId
                  << " frame " << frameNo << ": " << e.what() << "\n");
    }
}

void process_band_split_raw(const uint8_t *frameBuffer, size_t bufferSize,
                            int bandId, int frameNo, int activeIndex,
                            std::ofstream &foutL, std::ofstream &foutR)
{
    try
    {

        if (!foutL.is_open() || !foutR.is_open())
        {
            PRINT_LOG( "[E]", "Output files not open for band " << bandId << "\n");
            return;
        }

        size_t lineSize = commonParams.Width * 10 / 8;
        size_t halfLineSize = lineSize / 2;
        size_t bandOffset = static_cast<uint64_t>(commonParams.Width) * commonParams.Height * frameNo * 10 / 8 +
                            static_cast<uint64_t>(commonParams.Width) * commonParams.bandHeight * activeIndex * 10 / 8;

        size_t requiredSize = bandOffset + lineSize * commonParams.bandHeight;
        if (requiredSize > bufferSize)
        {
            PRINT_LOG( "[E]", "Out-of-bounds access in band " << bandId
                      << " frame " << frameNo << ". Need " << requiredSize
                      << " but buffer is " << bufferSize << "\n");
            return;
        }

        const uint8_t *bandData = frameBuffer + bandOffset;

        std::lock_guard<std::mutex> lock(file_mutexes[bandId]);

        for (int i = 0; i < commonParams.bandHeight; ++i)
        {
            const uint8_t *linePtr = bandData + i * lineSize;
            foutL.write(reinterpret_cast<const char *>(linePtr), halfLineSize);
            foutR.write(reinterpret_cast<const char *>(linePtr + halfLineSize), halfLineSize);
        }
    }
    catch (const std::exception &e)
    {
        PRINT_LOG( "[E]", "Band " << bandId << " Frame " << frameNo << ": " << e.what() << "\n");
    }
}

void splitRawFile(std::ofstream &out, const uint8_t *rawBuffer, size_t frameSize, int startFrame, int endFrame)
{
    for (int i = startFrame; i <= endFrame; ++i)
    {
        const uint8_t *framePtr = rawBuffer + static_cast<size_t>(i) * frameSize;
        out.write(reinterpret_cast<const char *>(framePtr), frameSize);
        if (!out)
        {
            PRINT_LOG( "[E]", "Failed to write frame " << i << "\n");
            break;
        }
    }
}

void writeEphemerisCircular(std::ofstream &metaOut,
                            const std::vector<AdcsEphemerisData> &dataVec,
                            size_t &currentIndex,
                            size_t countToWrite)
{
    if (dataVec.empty()) return; // nothing to write

    const uint16_t dataSize = sizeof(AdcsEphemerisData);
    size_t remainingToWrite = countToWrite;
    size_t vecSize = dataVec.size();

    while (remainingToWrite > 0)
    {
        uint16_t index = static_cast<uint16_t>(currentIndex); // wrap into 2 bytes
        metaOut.write(reinterpret_cast<const char*>(&index), sizeof(index));
        metaOut.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
        metaOut.write(reinterpret_cast<const char*>(&dataVec[currentIndex]), dataSize);
        // std::cout << "Wrote ephemeris data for index: " << currentIndex <<" Index: "<<index<< "\n";
        currentIndex = (currentIndex + 1) % vecSize;
        remainingToWrite--;
    }
}

void printSingleAdcsEphemerisData(const AdcsEphemerisData &adcs)
{
    std::cout << "Orbit Time: " << adcs.orbit_time << " s\n";

    std::cout << "ECI Position (km): "
              << adcs.eci_position_x << ", "
              << adcs.eci_position_y << ", "
              << adcs.eci_position_z << "\n";

    std::cout << "ECI Velocity (km/s): "
              << adcs.eci_velocity_x << ", "
              << adcs.eci_velocity_y << ", "
              << adcs.eci_velocity_z << "\n";

    std::cout << "ECEF Position (km): "
              << adcs.ecef_position_x << ", "
              << adcs.ecef_position_y << ", "
              << adcs.ecef_position_z << "\n";

    std::cout << "ECEF Velocity (km/s): "
              << adcs.ecef_velocity_x << ", "
              << adcs.ecef_velocity_y << ", "
              << adcs.ecef_velocity_z << "\n";

    std::cout << "Angular Rate (deg/s): "
              << adcs.ang_rate_x << ", "
              << adcs.ang_rate_y << ", "
              << adcs.ang_rate_z << "\n";

    std::cout << "Attitude Quaternion: "
              << adcs.att_quat_1 << ", "
              << adcs.att_quat_2 << ", "
              << adcs.att_quat_3 << ", "
              << adcs.att_quat_4 << "\n";

    std::cout << "EPS Voltage: " << epsVoltage << "\n";
    std::cout << "Latitude: " << adcs.latitude << "\n";
    std::cout << "Longitude: " << adcs.longitude << "\n";
    std::cout << "Altitude: " << adcs.altitude << " km\n";

    std::cout << "Nadir Vector: " << adcs.nadir_vector_x << ", " << adcs.nadir_vector_y << ", " << adcs.nadir_vector_z << "\n";
    std::cout << "Geodetic Nadir Vector: " << adcs.gd_nadir_vector_x << ", " << adcs.gd_nadir_vector_y << ", " << adcs.gd_nadir_vector_z << "\n";
    std::cout << "Beta Angle: " << adcs.beta_angle << "\n";

    const char* flags[] = {
        "Time Validity", "ECI Pos Valid", "ECI Vel Valid", "ECEF Pos Valid",
        "ECEF Vel Valid", "Rate Valid", "Attitude Valid",
        "Lat-Lon-Alt Valid", "Nadir Vector Valid", "Geo Nadir Valid", "Beta Angle Valid"
    };

    std::cout << "Validity Flags:\n";
    for (int i = 0; i < static_cast<int>(sizeof(flags) / sizeof(flags[0])); ++i) {
        int bit = (adcs.validity_flags >> i) & 1;
        std::cout << "  " << flags[i] << ": " << bit << "\n";
    }
}

void printAllAdcsEphemerisData()
{
    for (size_t i = 0; i < gnss_eph_data_history.size(); ++i) {
        const auto &entry = gnss_eph_data_history[i];
        std::cout << "=== ADCS Ephemeris Data [" << i << "] ===\n";
        printSingleAdcsEphemerisData(entry);
    }
}


bool Stream::ProcessData()
{
    uint8_t *rawBuffer = reinterpret_cast<uint8_t *>(Param.file_memory);
    size_t rawDataSize = Param.file_size;
    if (rawDataSize == 0 || rawBuffer == nullptr)
    {
        PRINT_LOG( "[E46]", "No data to process binning or buffer is null.\n");
        return false;
    }
    size_t frameSize = commonParams.Width * commonParams.Height * 10 / 8;
    size_t totalFrames = rawDataSize / frameSize;
    commonParams.totalFrames = totalFrames;
    size_t bytesPerLine = commonParams.Width * 10 / 8;
    std::vector<int> activeBandIds;
    for (int i = 0; i < 7; ++i)
    {
        if (commonParams.regionModes[i])
            activeBandIds.push_back(i);
    }
    commonParams.bandHeight = (commonParams.Height - 1) / activeBandIds.size();
    PRINT_LOG( "[I88]", "Computed BAND_HEIGHT = " << commonParams.bandHeight << "\n");

    std::string folderTrimmed = folder;
    if (!folderTrimmed.empty() && folderTrimmed.back() == '/')
        folderTrimmed.pop_back();

    std::filesystem::path folderPath(folderTrimmed);
    std::string folderName = folderPath.filename().string();

    // if (G_Binning < 128 && G_Binning >= 0)
    // {
    std::vector<std::ofstream> bandFiles(7), bandSplitLeft(7), bandSplitRight(7);
    for (int band = 0; band < 7; ++band)
    {
        if (commonParams.regionModes[band])
        {
            std::filesystem::path basePath = folderPath / (folderName + ".band" + std::to_string(band));

            if (commonParams.binningStatus[band])
            {
                std::filesystem::path filePath = basePath;
                filePath += "2";
                bandFiles[band].open(filePath, std::ios::binary | std::ios::trunc);
                if (!bandFiles[band])
                {
                    PRINT_LOG( "[E47]", "Failed to open file: " << filePath << "\n");
                    return 0;
                }
            }
            else
            {
                std::filesystem::path filePathL = basePath;
                filePathL += "0";
                std::filesystem::path filePathR = basePath;
                filePathR += "1";

                bandSplitLeft[band].open(filePathL, std::ios::binary | std::ios::trunc);
                bandSplitRight[band].open(filePathR, std::ios::binary | std::ios::trunc);

                if (!bandSplitLeft[band] || !bandSplitRight[band])
                {
                    PRINT_LOG( "[E48]", "Failed to open split files: " << filePathL << " / " << filePathR << "\n");
                    return 0;
                }
            }
        }
    }
    std::filesystem::path metaPath = folderPath / (folderName + ".meta");
    std::ofstream metaOut(metaPath, std::ios::binary | std::ios::trunc);
    if (!metaOut)
    {
        PRINT_LOG( "[E49]", "Failed to open output.meta\n");
        return 0;
    }
    PRINT_LOG("","Number of ADCS Ephemeris Data Stored: "<<gnss_eph_data_history.size() <<" and structure size: "<<
                sizeof(gnss_eph_data_history[0])<<"("<<sizeof(AdcsEphemerisData)<< "). Per each Image metadata, appending: "<<
                ephWriteCount<<" ADCS Ephemeris Data.\n");
    // printAllAdcsEphemerisData();


    auto start = std::chrono::high_resolution_clock::now();
    for (size_t frame = 0; frame < totalFrames; ++frame)
    {
        size_t frameOffset = static_cast<uint64_t>(commonParams.Width) * commonParams.Height * frame * 10 / 8;
        size_t lastLineOffset = frameOffset + ((commonParams.Height - 1) * bytesPerLine);
        if (lastLineOffset + 56 <= rawDataSize)
        {
            metaOut.write(reinterpret_cast<const char *>(&rawBuffer[lastLineOffset]), 56);
            // Write ephemeris data with wrap-around
            writeEphemerisCircular(metaOut, gnss_eph_data_history, ephIndex, ephWriteCount);
        }
        else
        {
            PRINT_LOG( "[E50]", "Frame " << frame << ": insufficient data for metadata.\n");
        }

        std::vector<std::thread> threads;
        for (int i = 0; i < activeBandIds.size(); ++i)
        {
            int bandId = activeBandIds[i];
            if (commonParams.binningStatus[bandId])
            {
                threads.emplace_back(process_band_bin, rawBuffer, rawDataSize,
                                        bandId, frame, i, std::ref(bandFiles[bandId]));
            }
            else
            {
                threads.emplace_back(process_band_split_raw, rawBuffer, rawDataSize,
                                        bandId, frame, i,
                                        std::ref(bandSplitLeft[bandId]), std::ref(bandSplitRight[bandId]));
            }
        }
        for (auto &t : threads)
            t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    // PRINT_LOG( "Total processing time: " << elapsed << " s\n");

    for (auto &f : bandFiles)
        if (f.is_open())
            f.close();
    for (auto &f : bandSplitLeft)
        if (f.is_open())
            f.close();
    for (auto &f : bandSplitRight)
        if (f.is_open())
            f.close();
    metaOut.close();
    // }
    // else
    // {
    //     std::cout << "[I89] G_Binning: "<<G_Binning<<" is not set for binning. Splitting raw data into two files.\n";
    //     std::vector<std::ofstream> rawSplitLeft(1), rawSplitRight(1);
    //     std::filesystem::path filePathL = folderPath / (folderName + ".raw0");
    //     std::filesystem::path filePathR = folderPath / (folderName + ".raw1");

    //     rawSplitLeft[0].open(filePathL, std::ios::binary | std::ios::trunc);
    //     rawSplitRight[0].open(filePathR, std::ios::binary | std::ios::trunc);

    //     if (!rawSplitLeft[0] || !rawSplitRight[0])
    //     {
    //         PRINT_LOG( "[E48] Failed to open split files: " << filePathL << " / " << filePathR << "\n");
    //         return 0;
    //     }
    //     int startFrame1 = 0, endFrame1 = 0, startFrame2 = 0;

    //     if (totalFrames == 1) {
    //         endFrame1 = 1;
    //         startFrame2 = -1;
    //     } else if (totalFrames < static_cast<size_t>(OVERLAP)) {
    //         startFrame2 = totalFrames / 2 + 1;
    //         endFrame1 = startFrame2 - 1;
    //     } else {
    //         if (totalFrames % 2 != 0)
    //             endFrame1 = (totalFrames / 2 + 1) + OVERLAP - 1;
    //         else
    //             endFrame1 = (totalFrames / 2) + OVERLAP - 1;

    //         startFrame2 = endFrame1 - OVERLAP * 2 + 1;
    //     }

    //     if (endFrame1 > totalFrames || startFrame2 >= totalFrames || startFrame2 < 0)
    //     {
    //         PRINT_LOG( "[E51] Invalid frame range computed: "
    //                   << "endFrame1=" << endFrame1 << ", startFrame2=" << startFrame2
    //                   << ", totalFrames=" << totalFrames << "\n");
    //     }
    //     std::thread raw0(splitRawFile, std::ref(rawSplitLeft[0]), rawBuffer, frameSize, startFrame1, endFrame1);
    //     std::thread raw1(splitRawFile, std::ref(rawSplitRight[0]), rawBuffer, frameSize, startFrame2, totalFrames - 1);
    //     raw0.join();
    //     raw1.join();
    //     rawSplitLeft[0].close();
    //     rawSplitRight[0].close();
    //     PRINT_LOG( "[I90] Raw data split done:\n");
    //     PRINT_LOG( "  " << filePathL << " [Frames " << startFrame1 << " to " << endFrame1 << "]\n");
    //     PRINT_LOG( "  " << filePathR << " [Frames " << startFrame2 << " to " << totalFrames - 1 << "]\n");
    // }
    free(Param.file_memory); // or just free() if malloc was used
    Param.file_memory = nullptr;
    // PRINT_LOG("", "Before clearing: count=" << gnss_eph_data_history.size());
    gnss_eph_data_history.clear();
    ephIndex = 0;
    gnss_eph_data_history.shrink_to_fit();
    // PRINT_LOG("", "After clearing: count=" << gnss_eph_data_history.size());
    return 1;
}

void Stream::saveCommonConfig()
{
    std::string folderTrimmed = folder;
    if (!folderTrimmed.empty() && folderTrimmed.back() == '/')
    {
        folderTrimmed.pop_back();
    }
    std::filesystem::path folderPath(folderTrimmed);
    std::string folderName = folderPath.filename().string();
    json config;
    config["FolderPath"] = folderName;
    std::string configCommonPath = std::string(acquisitionPath) + "commonConfig.json";
    std::ofstream outFile(configCommonPath);
    if (!outFile)
    {

        PRINT_LOG("[E43]", "Failed to open JSON file for writing current folder name: " << configCommonPath << std::endl);
        return;
    }
    outFile << config.dump(4);
    outFile.close();

    PRINT_LOG("[I80]", "Common Configuration saved to JSON successfully." << std::endl);
}
void Stream::transferFile(const std::string& logPath)
{
    char CurrentFolder[Max_File_Path_Len];
    if (folder.empty())
    {

        std::cout<< "No captures and folder path is empty" << endl;
        time_t now = time(NULL);
        struct tm *t = localtime(&now);

        sprintf(CurrentFolder, "%sAcq%06d_%02d%02d%02d%02d%02d%04d/", acquisitionPath.c_str(), G_Task, t->tm_sec, t->tm_min, t->tm_hour, t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);
        if (mkdir(CurrentFolder, 0777) == -1)
        {

            if (errno == EEXIST)
            {
                std::cout<< "Directory already exists: " << CurrentFolder;
            }
            else
            {
                std::cout<< "Error creating directory " << CurrentFolder << ": " << strerror(errno);
            }
        }
        else
        {

            std::cout<< "Directory created: " << CurrentFolder;
        }
        folder = CurrentFolder;
    }
    std::string folderTrimmed = folder;
    if (!folderTrimmed.empty() && folderTrimmed.back() == '/')
    {
        folderTrimmed.pop_back();
    }
    std::filesystem::path folderPath(folderTrimmed);
    std::string folderName = folderPath.filename().string();

    std::cout<< "folder path is: " << folderPath << " and folder name is: " << folderName << std::endl;

    std::filesystem::path source = logPath;
    std::string extension = source.extension().string();

    std::string renamedLogFile = folderName + extension;

    // Final destination path
    std::filesystem::path destination = folderPath / renamedLogFile;

    std::cout<< "Destination path (renamed): " << destination << std::endl;
    try
    {
        // Move the file
        std::filesystem::rename(source, destination);

        std::cout<< "File moved successfully to " << destination << "\n";
    }
    catch (const std::filesystem::filesystem_error &e)
    {

        std::cout<< "Error: " << e.what() << '\n';
    }
}
