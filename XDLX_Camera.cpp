#include "GenericParam.h"

GlobalParams commonParams;

char CamSettingsZipW[Max_File_Path_Len] = "/home/xdlinx/Pictures/XDLX_KYFG_V6_1_Dev/ConfigData/camera.zip";
char CamSettingsXmlW[Max_File_Path_Len] = "/home/xdlinx/Pictures/XDLX_KYFG_V6_1_Dev/ConfigData/camera.xml";
char CamSettingsPathR[Max_File_Path_Len] = "/home/xdlinx/Pictures/XDLX_KYFG_V6_1_Dev/ConfigData/camera.zip";

void ValidateSettingPathR(char *CamSettingsPathR)
{
    FILE *Fp;
    
    PRINT_LOG("[I14]", "Camera Settings path: "<< CamSettingsPathR<<endl);
    Fp = fopen(CamSettingsPathR, "rb");
    if (Fp == NULL)
    {
        CamSettingsPathR[0] = '\0';
        
        PRINT_LOG("[E09]", "Invalid Camera Settings Path\n");
    }
    else
        fclose(Fp);
}

class Camera
{
private:
    CAMHANDLE camHandles[KY_MAX_CAMERAS]; // there are maximum KY_MAX_CAMERAS cameras
    KYBOOL camConnected[KY_MAX_CAMERAS];
    KYFGCAMERA_INFO2 *camInfos = NULL;
    int nDetected;
    int nConnected;

public:
    Camera();
    int Detect(FGHANDLE fgHandle);
    int Connect(FGHANDLE fgHandle, int CameraID);
    int ConnectDesired(FGHANDLE fgHandle, unsigned char CamMask);
    int ConnectDesiredRegion(CAMHANDLE camHandle, unsigned char CamMask);
    KYBOOL IsConnected(int CameraID);
    CAMHANDLE Handle(int CameraID); //--
    int SettingsDefault(FGHANDLE fgHandle, CAMHANDLE camHandle);
    int SettingsJSON(FGHANDLE fgHandle, CAMHANDLE camHandle);
    int SettingUserInput(FGHANDLE fgHandle, CAMHANDLE camHandle);
    int getSettingsApplied(FGHANDLE fgHandle, CAMHANDLE camHandle);
    void Display(KYFGCAMERA_INFO2 *cameraInfo);
    void ReadSettings(void); //--
    int StartCamera(unsigned int grabberIndex, int CamID, STREAM_HANDLE cameraStreamHandle, unsigned int FramesN);
    bool Close(int CameraID);
    int CloseAll();
    bool StopAll();
};

KYBOOL Camera::IsConnected(int CameraID)
{
    return (camConnected[CameraID]);
}

CAMHANDLE Camera::Handle(int CameraID)
{
    if (this->camConnected[CameraID] != KYFALSE)
        return (camHandles[CameraID]);
    return INVALID_CAMHANDLE;
}


int Camera::StartCamera(unsigned int grabberIndex, int CamID, STREAM_HANDLE cameraStreamHandle, unsigned int FramesN)
{
    
    PRINT_LOG("", "Starting Camera= "<< CamID<<endl);
    fflush(stdout);
    // put all buffers to input queue
    if(FGSTATUS_OK != KYFG_BufferQueueAll(cameraStreamHandle, KY_ACQ_QUEUE_UNQUEUED, KY_ACQ_QUEUE_INPUT)){
        
        PRINT_LOG("[E33]", "Error: KYFG_BufferQueueAll for CamID: "<<CamID<<std::endl);
    }

    // start acquisition
    if(FGSTATUS_OK != KYFG_CameraStart(this->camHandles[CamID], cameraStreamHandle, FramesN)){
        
        PRINT_LOG("[E34]", "Error: KYFG_CameraStart for CamID: "<<CamID<<std::endl);
    }

    return 0;
}

Camera ::Camera()
{
    for (int i = 0; i < KY_MAX_CAMERAS; i++)
    {
        this->camConnected[i] = KYFALSE;
    }
    this->nDetected = 0;
    this->nConnected = 0;
    
    PRINT_LOG( "[I03]", "Camera Initialized...done..." << std::endl);
}

int Camera::Detect(FGHANDLE fgHandle)
{
                
    PRINT_LOG("[I10]", "Camera Detection:\n");
    this->nDetected = _countof(this->camHandles);

    if (FGSTATUS_OK != KYFG_UpdateCameraList(fgHandle, this->camHandles, &(this->nDetected)))
    {
        
        PRINT_LOG("[E07]", "Camera detect error. Please try again\n");
        return -1;
    }

    if (!(this->nDetected))
    {
        
        PRINT_LOG("[E08]", "No cameras detected. Please connect at least one camera\n");
        return -1; // No cameras were detected
    }
    
    PRINT_LOG("[I11]", "Number of cameras detected to the current Grabber: "<<this->nDetected<<"(<"<<KY_MAX_CAMERAS<<"(Max))\n");

    if (camInfos)
    {
        free(camInfos);
    }

    camInfos = (KYFGCAMERA_INFO2 *)malloc(this->nDetected * sizeof(KYFGCAMERA_INFO2));

    for (int i = 0; i < this->nDetected; i++)
    {
        camInfos[i].version = 1;
        KYFG_CameraInfo2(camHandles[i], &camInfos[i]);
    }

    for (int i = 0; i < this->nDetected; i++)
    {
        
        PRINT_LOG("[I12]", "Camera["<<i<<"]: deviceModelName= "<<camInfos[i].deviceModelName<<endl);
    }
    return (this->nDetected);
}

void Camera::Display(KYFGCAMERA_INFO2 *cameraInfo)
{
    if (cameraInfo != NULL)
    {
        PRINT_LOG("", endl);
        PRINT_LOG("", "[ 1] Version= " << cameraInfo->version << endl);
        PRINT_LOG("", "[ 2] master_link= " << cameraInfo->master_link << endl);
        PRINT_LOG("", "[ 3] link_mask= " << cameraInfo->link_mask << endl);
        PRINT_LOG("", "[ 4] link_speed= " << cameraInfo->link_speed << endl);
        PRINT_LOG("", "[ 5] deviceVersion= " << cameraInfo->deviceVersion << endl);
        PRINT_LOG("", "[ 6] deviceVendorName= " << cameraInfo->deviceVendorName << endl);
        PRINT_LOG("", "[ 7] deviceManufacturerInfo= " << cameraInfo->deviceManufacturerInfo << endl);
        PRINT_LOG("", "[ 8] deviceModelName= " << cameraInfo->deviceModelName << endl);
        PRINT_LOG("", "[ 9] deviceID= " << cameraInfo->deviceID << endl);
        PRINT_LOG("", "[10] deviceUserID= " << cameraInfo->deviceUserID << endl);
        PRINT_LOG("", boolalpha << "[11] outputCamera= " << cameraInfo->outputCamera << endl);
        PRINT_LOG("", boolalpha << "[12] virtualCamera= " << cameraInfo->virtualCamera << endl);
        PRINT_LOG("[13]", "deviceFirmwareVersion= " << cameraInfo->deviceFirmwareVersion << endl);
    }
}

void Camera::ReadSettings() // Start From Here
{
    char *buffer;
    uint64_t bufferSize = 0;
    KYBOOL isZip = KYFALSE;
    FILE *fileOut = NULL;
    bufferSize = 0; // Get the size of the buffer to allocate.

    if (FGSTATUS_OK == KYFG_CameraGetXML(this->camHandles[0], NULL, &isZip, &bufferSize))
    {
        
        PRINT_LOG( "", "Camera Settings Data Size= " << bufferSize << endl);
        buffer = (char *)malloc(bufferSize + 2); // allocate memory for buffer
        // extract camera’s native XML file
        if (FGSTATUS_OK == KYFG_CameraGetXML(this->camHandles[0], buffer, &isZip, &bufferSize))
        {
            if (KYTRUE == isZip)
                fileOut = fopen(CamSettingsZipW, "wb"); // camera Settings in zip format
            else
                fileOut = fopen(CamSettingsXmlW, "wb"); // camera Settings in xml format
            if (NULL != fileOut)
            {
                fwrite(buffer, bufferSize, 1, fileOut);
                fclose(fileOut);
            }
        }
        if (buffer != NULL)
            free(buffer); // free buffer after use
    }
}

int Camera::Connect(FGHANDLE fgHandle, int CameraID)
{
    
    PRINT_LOG("", "Connecting the camera["<<CameraID<<"]:\n");
    if (this->camConnected[CameraID] == KYFALSE)
    {
        bool ValidIndex = NumberInRangeInput(0, this->nDetected - 1, &CameraID, "is Invalid Camera Index and not connected");
        if (ValidIndex)
        {
            Display(&camInfos[CameraID]);
            ValidateSettingPathR(CamSettingsPathR);
            if (FGSTATUS_OK == KYFG_CameraOpen2(this->camHandles[CameraID], CamSettingsPathR)) // XML file path to be added in place of NULL pointer
            {
                (this->nConnected) += 1;
                
                PRINT_LOG("[I15]", "The Camera was connected successfully\n");
            }
            else{
                
                PRINT_LOG("[E10]", "The Camera was not connected\n");
            }
        }
    }
    else{
        
        PRINT_LOG("[I16]", "Camera is already connected\n ");
    }
    return (this->nConnected);
}



int Camera::ConnectDesired(FGHANDLE fgHandle, unsigned char CamMask)
{
    unsigned char mask=1;
    
    PRINT_LOG("[I13]", "Connecting the desired cameras:\n");

    for (int CameraID = 0; CameraID < this->nDetected; CameraID++) // Connect All the cameras
    {
        // if((CameraID == 0) || (CamMask & mask))
        if((CameraID == 0))
        {
            
            PRINT_LOG("[I14]", "Camera["<<CameraID<<"]: ");
            if(CameraID == 0) {
                PRINT_LOG("", "Primary Camera\n");
            }
            if (this->camConnected[CameraID] == KYFALSE)
            {
                bool ValidIndex = NumberInRangeInput(0, this->nDetected - 1, &CameraID, "is Invalid Camera Index and not connected");
                if (ValidIndex)
                {
                    // Display(&camInfos[CameraID]);
                    ValidateSettingPathR(CamSettingsPathR);
                    if (FGSTATUS_OK == KYFG_CameraOpen2(this->camHandles[CameraID], CamSettingsPathR)) // XML file path to be added in place of NULL pointer
                    {
                        this->nConnected += 1;
                        this->camConnected[CameraID] = KYTRUE;
                        
                        PRINT_LOG("[I15]", "The Camera was connected successfully \n");
                    }
                    else{
                        
                        PRINT_LOG("[E10]", "The Camera was not connected\n");
                    }
                }
            }
            else{
                
                PRINT_LOG("[I16]", "Camera is already connected\n");
            }
                
        }
        mask= mask<<1;
    }
    return (this->nConnected);
}

int Camera::ConnectDesiredRegion(CAMHANDLE camHandle, unsigned char CamMask)
{
    int flag=0;
    unsigned char mask=1;
    uint64_t mode;
    
    PRINT_LOG("[I29]", "Connecting the desired Regions\n");

    for (int CameraID = 0; CameraID < 7; CameraID++) // Connect All the cameras
    {
        //if((CameraID == 0) || (CamMask & mask))
        std::string  regionMode = "Region" + std::to_string(CameraID) + "Mode";
        const char* regionModeCStr = regionMode.c_str();
        bool SetCam = (CamMask & mask)? true: false;
        FGSTATUS status = KYFG_SetCameraValueEnum(camHandle, regionModeCStr, SetCam );
        if(status == FGSTATUS_OK){
            
            PRINT_LOG("[I30]", "Camera: "<<CameraID<<" has turned "<<((SetCam)? "on" : "off"));
            // this->camConnected[CameraID] = SetCam;
            mode = KYFG_GetCameraValueEnum(camHandle, regionModeCStr );
            PRINT_LOG("", " "<<regionModeCStr<<": "<<mode<<endl);
            flag++;
        }
        else {
            
            PRINT_LOG("[E18]", "Camera: "<<CameraID<<" failed to set "<<((SetCam)? "on" : "off")<<endl);
            flag--;
        }  
        mask= mask<<1;
    }
    
    return flag;
}

bool Camera::StopAll() // Grabber CurrentGrabber)
{
    PRINT_LOG("", "Closing all the cameras...\n");
    bool flag = true;
    for (int i = 0; i < this->nDetected; i++)
    {
        if (this->camHandles[i] != INVALID_CAMHANDLE)
        {
            if (FGSTATUS_OK == KYFG_CameraStop(this->camHandles[i])){
                PRINT_LOG("", "Camera["<<i<<"]: stoped\n");
            }
            else
            {
                PRINT_LOG("", "Camera["<<i<<"d]: could not stop\n");
                flag = false;
            }
        }
        else
        {
            PRINT_LOG("", "Invalid camera handle\n");
            flag = false;
        }
    }
    return flag;
}


int Camera::SettingsDefault(FGHANDLE fgHandle, CAMHANDLE camHandle){
    FGSTATUS status;
    
    PRINT_LOG("[I17]", "Default settings are being applied...\n");

    unsigned char mask=1;
    uint64_t mode;
    
    unsigned char CamMask = 127;
    for (int CameraID = 0; CameraID < 7; CameraID++) // Connect All the cameras
    {
        //if((CameraID == 0) || (CamMask & mask))
        std::string  regionMode = "Region" + std::to_string(CameraID) + "Mode";
        const char* regionModeCStr = regionMode.c_str();
        bool SetCam = (CamMask & mask)? true: false;
        FGSTATUS status = KYFG_SetCameraValueEnum(camHandle, regionModeCStr, SetCam );
        if(status == FGSTATUS_OK){
            
            // PRINT_LOG("[I30] Camera: "<<CameraID<<" has turned "<<((SetCam)? "on" : "off");
            // this->camConnected[CameraID] = SetCam;
            mode = KYFG_GetCameraValueEnum(camHandle, regionModeCStr );
            // PRINT_LOG(" "<<regionModeCStr<<": "<<mode<<endl);
        }
        else {
            PRINT_LOG("[E18]", "Camera: "<<CameraID<<" failed to set "<<((SetCam)? "on" : "off")<<endl);
        }  
        mask= mask<<1;
    }

    status = KYFG_SetCameraValueEnum(camHandle, "TDIMode", (int64_t) G_TDI_Modes);
    if(status != FGSTATUS_OK)
        PRINT_LOG("[E11]", "Failed to set default TDIMode  ");
    G_TDI_Modes= KYFG_GetCameraValueInt(camHandle, "TDIMode");
    // printf("\nTDI_Modes=%d ", G_TDI_Modes);
    
    PRINT_LOG("[I18]", "Applied default TDI_Modes="<<(int64_t)G_TDI_Modes<<endl);

    if(G_TDI_Modes == 2){
        status = KYFG_SetCameraValueInt(camHandle, "TDIStages", (int64_t)G_TDI_Stage);
        if(status != FGSTATUS_OK)
            PRINT_LOG("[E12]", "Failed to set default TDIStages ");
        G_TDI_Stage= KYFG_GetCameraValueInt(camHandle, "TDIStages");
        
        PRINT_LOG("[I19]", "Applied default TDI_Stages="<<(int64_t)G_TDI_Stage<<endl);
    }

    status = KYFG_SetCameraValueFloat(camHandle, "AcquisitionFrameRate",G_FPS);
    if(status != FGSTATUS_OK)
        PRINT_LOG("[E13]", "Failed to set default FPS ");
    G_FPS = KYFG_GetCameraValueFloat(camHandle, "AcquisitionFrameRate");
    
    PRINT_LOG("[I20]", "Applied default FPS = "<<G_FPS<<endl);

    status = KYFG_SetCameraValueFloat(camHandle, "ExposureTime",G_ExpTime);
    if(status != FGSTATUS_OK)
        PRINT_LOG("[E14]", "Failed to set default ExposureTime ");
    G_ExpTime = KYFG_GetCameraValueFloat(camHandle, "ExposureTime");
    
    PRINT_LOG("[I21]", "Applied dafault ExposureTime = "<<G_ExpTime<<endl);

    status = KYFG_SetCameraValueFloat(camHandle, "Gain",G_Gain);
    if(status != FGSTATUS_OK)
        PRINT_LOG("[E15]", "Failed to set default Gain ");
    G_Gain = KYFG_GetCameraValueFloat(camHandle, "Gain");
    
    PRINT_LOG("[I22]", "Applied default Gain = "<<G_Gain<<endl);

    status = KYFG_SetCameraValueInt(camHandle, "BandXShift",G_BandXShift);
    if(status != FGSTATUS_OK)
        PRINT_LOG("[E16]", "Failed to set default BandXShift ");
    G_BandXShift = KYFG_GetCameraValueInt(camHandle, "BandXShift");
    
    PRINT_LOG("[I23]", "Applied default BandXShift = "<<G_BandXShift<<endl);

    // status = KYFG_SetCameraValueInt(camHandle, "BandYShift",G_BandYShift);
    // if(status != FGSTATUS_OK)
    //     PRINT_LOG("[E17] Failed to set default BandYShift ");
    // G_BandYShift = KYFG_GetCameraValueInt(camHandle, "BandYShift");
    
    // PRINT_LOG("[I24] Applied default BandYShift = "<<G_BandYShift<<std::endl);
    
    if(FGSTATUS_OK !=  KYFG_SetCameraValueBool(camHandle, "ChunkModeActive", true)){
        PRINT_LOG("[E]", "Error: Set ChunkModeActive failed"<<std::endl);
    } //enable the generation of Metadata line
    bool ChunkModeActive = KYFG_GetCameraValueBool(camHandle, "ChunkModeActive"); //enable the generation of Metadata line
    PRINT_LOG("[I]", "ChunkModeActive= "<<ChunkModeActive<<std::endl);

    return 0;
}

int Camera::SettingUserInput(FGHANDLE fgHandle, CAMHANDLE camHandle){
    FGSTATUS status;
    std::cout<<endl;
    PRINT_LOG("[I33]", "User settings ar being applied...\n");

    if(G_TotArg>7){
        status = KYFG_SetCameraValueEnum(camHandle, "TDIMode", (int64_t) G_TDI_Modes);
        PRINT_LOG("[I34]", "Set TDI_Modes="<<(int64_t)G_TDI_Modes);
        if (status == FGSTATUS_OK)
            PRINT_LOG("", " Passed...");
        else
            PRINT_LOG("", " Failed...");

        G_TDI_Modes= KYFG_GetCameraValueInt(camHandle, "TDIMode");
        PRINT_LOG("", " Applied TDI_Modes="<<(int64_t)G_TDI_Modes<<endl);
    
        if(G_TDI_Modes == 2){
            status = KYFG_SetCameraValueInt(camHandle, "TDIStages", (int64_t)G_TDI_Stage);
            PRINT_LOG("[I35]", "Set TDI_Stages= "<<(int64_t)G_TDI_Stage);
            if (status == FGSTATUS_OK)
                PRINT_LOG("", " Passed...");
            else
                PRINT_LOG("", " Failed...");

            G_TDI_Stage= KYFG_GetCameraValueInt(camHandle, "TDIStages");
            PRINT_LOG("", " Applied TDI_Stages="<<(int64_t)G_TDI_Stage<<endl);
            int InTDIYShift= KYFG_GetCameraValueInt(camHandle, "TDIYShift");
#ifdef TDIYSHIFT
            // PRINT_LOG("Default TDIYShift:"<<InTDIYShift);
            if(G_TDIYShift <= InTDIYShift ){
                KYFG_SetCameraValueInt(camHandle, "TDIYShift", G_TDIYShift);
                G_TDIYShift= KYFG_GetCameraValueInt(camHandle, "TDIYShift");
                // PRINT_LOG(" G_TDIYShift after set:"<<G_TDIYShift);
            }else{
                PRINT_LOG("[E]", "G_TDIYShift: "<<G_TDIYShift<<" is greater than default TDIYShift: "<<InTDIYShift<<". Value should less than default TDIYShift.\n");
            }
            PRINT_LOG("[I35]", "Applied TDIYShift="<<G_TDIYShift<<" [Default="<<InTDIYShift<<"]"<<endl);
#endif

        }
        if(G_TDI_Modes == 1){
            int FrameCountMax = KYFG_GetCameraValueInt(camHandle, "TDIFrameCountMax");
            PRINT_LOG("[I36]", "Set FrameCount="<<FrameCountMax<<" [MaxFrameCount= "<<FrameCountMax<<"]");
            status = KYFG_SetCameraValueInt(camHandle, "TDIFrameCount", FrameCountMax);
            if (status == FGSTATUS_OK)
                PRINT_LOG("", " Passed...");
            else
                PRINT_LOG("", " Failed...");
            FrameCountMax = KYFG_GetCameraValueInt(camHandle, "TDIFrameCount");
            TotalFrames = FrameCountMax;
            G_Duration = 10; //Time required to capture max frames.
            PRINT_LOG("", " Applied FrameCount = "<<FrameCountMax<<endl);
        }
    }

    if(G_TotArg>8){
        double AcqFrameRateMax = KYFG_GetCameraValueFloat(camHandle, "AcquisitionFrameRateMax");
        PRINT_LOG("[I37]", "Set FPS="<<G_FPS<<" [MaxFPS= "<<AcqFrameRateMax<<"]");
        if(G_FPS > AcqFrameRateMax) G_FPS = AcqFrameRateMax-0.01;
        status = KYFG_SetCameraValueFloat(camHandle, "AcquisitionFrameRate",G_FPS);
        if (status == FGSTATUS_OK)
            PRINT_LOG("", " Passed...");
        else
            PRINT_LOG("", " Failed...");
        G_FPS = KYFG_GetCameraValueFloat(camHandle, "AcquisitionFrameRate");
        PRINT_LOG("", " Applied FPS = "<<G_FPS<<endl);
    }

    if(G_TotArg>9){
        double ExposureTimeMax = KYFG_GetCameraValueFloat(camHandle, "ExposureTimeMax");
        PRINT_LOG("[I38]", "Set Exposure Time="<<G_ExpTime<<" [MaxExpTime= "<<ExposureTimeMax<<"]");
        if(G_ExpTime > ExposureTimeMax) G_ExpTime = ExposureTimeMax;
        status = KYFG_SetCameraValueFloat(camHandle, "ExposureTime",G_ExpTime);
        if (status == FGSTATUS_OK)
            PRINT_LOG("", " Passed...");
        else
            PRINT_LOG("", " Failed...");
        G_ExpTime = KYFG_GetCameraValueFloat(camHandle, "ExposureTime");
        PRINT_LOG("", " Applied ExposureTime = "<<G_ExpTime<<endl);
    }

    if(G_TotArg>10){
        status = KYFG_SetCameraValueFloat(camHandle, "Gain",G_Gain);
        PRINT_LOG("[I39]", "Set Gain="<<G_Gain);
        if (status == FGSTATUS_OK)
            PRINT_LOG("", " Passed...");
        else
            PRINT_LOG("", " Failed...");
        G_Gain = KYFG_GetCameraValueFloat(camHandle, "Gain");
        PRINT_LOG("", " Applied Gain = "<<G_Gain<<endl);
    }

    if(G_TotArg>11){
        status = KYFG_SetCameraValueInt(camHandle, "BandXShift",G_BandXShift);
        PRINT_LOG("[I40]", "Set BandXShift="<<G_BandXShift);
        if (status == FGSTATUS_OK)
            PRINT_LOG("", " Passed...");
        else
            PRINT_LOG("", " Failed...");
        G_BandXShift = KYFG_GetCameraValueInt(camHandle, "BandXShift");
        PRINT_LOG("", " Applied BandXShift = "<<G_BandXShift<<endl);
    }

    // if(G_TotArg>12){
    //     status = KYFG_SetCameraValueInt(camHandle, "BandYShift",G_BandYShift);
    //     PRINT_LOG("[I41] Set BandYShift="<<G_BandYShift);
    //     if (status == FGSTATUS_OK)
    //         PRINT_LOG(" Passed...");
    //     else
    //         PRINT_LOG(" Failed...");
    //     G_BandYShift = KYFG_GetCameraValueInt(camHandle, "BandYShift");
    //     PRINT_LOG(" Applied BandYShift = "<<G_BandYShift<<std::endl);
    // }

    return 0;
}

int Camera::getSettingsApplied(FGHANDLE fgHandle, CAMHANDLE camHandle){
    
    PRINT_LOG("[I42]", "Final Settings applied:\n");
    unsigned char mask = 1;
    commonParams.regionModes.clear();
    commonParams.binningStatus.clear();
    for (int CameraID = 0; CameraID < 7; CameraID++) // Connect All the cameras
    {
        
        std::string  regionMode = "Region" + std::to_string(CameraID) + "Mode";
        const char* regionModeCStr = regionMode.c_str();
        uint64_t mode = KYFG_GetCameraValueEnum(camHandle, regionModeCStr );
        commonParams.regionModes.push_back((uint8_t)mode);
        PRINT_LOG("[I43]", std::string(regionModeCStr)+":"+std::to_string(mode)+"\n");

        //Get Binning Status
        bool binStatus = (G_Binning & mask)? true: false;
        commonParams.binningStatus.push_back((uint8_t)binStatus);

        mask= mask<<1;
    } 

    
    commonParams.Width = KYFG_GetCameraValueInt(camHandle, "Width");
    PRINT_LOG("[I44]", "Applied Width="<<commonParams.Width<<endl);
    
    commonParams.regionHeight = KYFG_GetCameraValueInt(camHandle, "RegionHeight");
    PRINT_LOG("[I45]", "Applied RegionHeight="<<commonParams.regionHeight<<endl);
    
    commonParams.Height = KYFG_GetCameraValueInt(camHandle, "Height");
    PRINT_LOG("[I46]", "Applied Height="<<commonParams.Height<<endl);
    
    G_TDI_Modes= KYFG_GetCameraValueInt(camHandle, "TDIMode");
    PRINT_LOG("[I47]", "Applied TDI_Modes="<<(int64_t)G_TDI_Modes<<endl);
    commonParams.TDIMode = (int64_t)G_TDI_Modes;
    
    G_TDI_Stage = KYFG_GetCameraValueInt(camHandle, "TDIStages");
    PRINT_LOG("[I48]", "Applied TDI_Stages="<<(int64_t)G_TDI_Stage<<endl);
    commonParams.TDIStages = (int64_t)G_TDI_Stage;

#ifdef TDIYSHIFT
    G_TDIYShift= KYFG_GetCameraValueInt(camHandle, "TDIYShift");
    PRINT_LOG("[I48]", "Applied G_TDIYShift: "<<G_TDIYShift<<endl);
#endif
    
    G_FPS = KYFG_GetCameraValueFloat(camHandle, "AcquisitionFrameRate");
    PRINT_LOG("[I49]", "Applied FPS = "<<G_FPS<<endl);
    commonParams.FPS = G_FPS;
    
    G_ExpTime = KYFG_GetCameraValueFloat(camHandle, "ExposureTime");
    PRINT_LOG("[I50]", "Applied ExposureTime = "<<G_ExpTime<<endl);
    commonParams.ExpTime = G_ExpTime;
    
    G_Gain = KYFG_GetCameraValueFloat(camHandle, "Gain");
    PRINT_LOG("[I51]", "Applied Gain = "<<G_Gain<<endl);
    commonParams.Gain = G_Gain;
    
    G_BandXShift = KYFG_GetCameraValueInt(camHandle, "BandXShift");
    PRINT_LOG("[I52]", "Applied BandXShift = "<<G_BandXShift<<endl);
    commonParams.BandXShift = G_BandXShift;
    
    // G_BandYShift = KYFG_GetCameraValueInt(camHandle, "BandYShift");
    // PRINT_LOG(" [I53] Applied BandYShift = "<<G_BandYShift<<endl);
    // commonParams.BandYShift = G_BandYShift;

    return 0;
}


int Camera::SettingsJSON(FGHANDLE fgHandle, CAMHANDLE camHandle)
{
    int line = 0;
    
    PRINT_LOG( "[I31]", "Setter and Getters through JSON...\n");
    std::string ConfigFile= userDirectory + "ConfigData/";
    switch(G_JSON)
    {
        case 0: ConfigFile += "CamConfig0.json";
                break;
        case 1: ConfigFile += "CamConfig1.json";
                break;
        case 2: ConfigFile += "CamConfig2.json";
                break;
        case 3: ConfigFile += "CamConfig3.json";
                break;
        case 4: ConfigFile += "CamConfig4.json";
                break;
        case 5: ConfigFile += "CamConfig5.json";
                break;
        case 6: ConfigFile += "CamConfig6.json";
                break;
        case 7: ConfigFile += "CamConfig7.json";
                break;
        case 8: ConfigFile += "CamConfig8.json";
                break;
        case 9: ConfigFile += "CamConfig9.json";
                break;
        case 10: ConfigFile += "CamConfig10.json";
                break;
        case 11: ConfigFile += "CamConfig11.json";
                break;
        case 12: ConfigFile += "CamConfig12.json";
                break;
        case 13: ConfigFile += "CamConfig13.json";
                break;
        case 14: ConfigFile += "CamConfig14.json";
                break;
        case 15: ConfigFile += "CamConfig15.json";
                break;
        default: ConfigFile += "CamConfig_default.json";
                break;
    }
                
    std::ifstream inputfile(ConfigFile);//"CamConfig.json");
    if (!inputfile.is_open())
    {
        
        PRINT_LOG( "[E19] ","Failed to open Configuration Settings file." <<ConfigFile<< std::endl);
        return -1;
    }
    else{
        
        PRINT_LOG("[I32]", "Configuration json file used: "<<ConfigFile<<endl);
    }
        
    json config;
    try
    {
        inputfile >> config; // Try parsing the JSON file
    }
    catch (const json::parse_error &e)
    {
        
        PRINT_LOG( "[E20]", "JSON parsing error: " << e.what() << std::endl);
        return -1; // or return some error code to indicate failure
    }

    if (config.contains("VislinkSettings"))
    {

        for (const auto &item : config["VislinkSettings"])
        {
            // Check for the presence of required data
            if (item.contains("Device") && item.contains("VariableName") && item.contains("Value"))
            {
                std::string var1 = item["Device"];
                std::string variableName = item["VariableName"];
                const char *var2 = variableName.c_str();
                std::string var3 = item["Value"];
                std::cout<<endl;
                
                PRINT_LOG( "", "JSON[" << line++ << "]= {" << var1 << ", " << var2 << ", " << var3 << "}; ");
                std::string dataTypes[9] = {"unknown", "int", "bool", "string", "double", "enum", "command", "register"};

                if (var1[0] == 'g' || var1[0] == 'G') // Only check first char i.e.[0] but JSON can be g|Grabber
                {                                     // Grabber

                    KY_CAM_PROPERTY_TYPE type = KYFG_GetGrabberValueType(fgHandle, var2);
                    PRINT_LOG( "", "Grabber: ");
                    if (var3 == "?")
                    { // Getters
                        PRINT_LOG( "", " get " << var2 << "(" << dataTypes[type + 1] << ")= ");
                        switch (type)
                        {
                        case 0:
                        {
                            int64_t valI = KYFG_GetGrabberValueInt(fgHandle, var2);
                            if (INVALID_INT_PARAMETER_VALUE == valI)
                            {
                                PRINT_LOG( "", "Invalid int parameter value: " << std::hex << "0X" << valI << ");" << std::dec);
                            }
                            else
                            {
                                PRINT_LOG( "", valI << ");");
                            }
                            break;
                        }
                        case 1:
                        {
                            bool valB = KYFG_GetGrabberValueBool(fgHandle, var2);
                            PRINT_LOG( "", valB << ");");
                            break;
                        }
                        case 2:
                        {

                            char *RecvStr = (char *)malloc(1); // Allocate 1 byte to start
                            if (RecvStr == nullptr)
                            {
                                PRINT_LOG( "", "Memory allocation failed." << std::endl);
                            }

                            uint32_t RecvLen = 0; // Initially set RecvLen to 1 (size of the first allocation)
                            FGSTATUS valS = KYFG_GetGrabberValueStringCopy(fgHandle, var2, RecvStr, &RecvLen);

                            if (valS == FGSTATUS_BUFFER_TOO_SMALL)
                            {
                                RecvStr = (char *)realloc(RecvStr, RecvLen); // Resize the buffer to the new size
                                if (RecvStr == nullptr)
                                {
                                    PRINT_LOG( "", "Memory reallocation failed." << std::endl);
                                }

                                // Try again to get the value with the resized buffer
                                valS = KYFG_GetGrabberValueStringCopy(fgHandle, var2, RecvStr, &RecvLen);
                            }

                            if (valS == FGSTATUS_OK)
                            {
                                if (RecvLen == 0)
                                {
                                    PRINT_LOG( "", "Empty String;");
                                }
                                else
                                {
                                    PRINT_LOG( "", RecvStr << ");");
                                }
                            }
                            else
                            {
                                PRINT_LOG( "", "Unable to get String due to:  " << std::hex << "0X" << valS << ");" << std::dec);
                            }

                            // Free the dynamically allocated memory after use
                            free(RecvStr);

                            break;
                        }
                        case 3:
                        {
                            double valF = KYFG_GetGrabberValueFloat(fgHandle, var2);
                            if (INVALID_FLOAT_PARAMETER_VALUE == valF)
                            {
                                PRINT_LOG( "", "Invalid Float parameter value: " << std::hex << "0X" << valF << ");" << std::dec);
                            }
                            else
                            {
                                PRINT_LOG( "", valF << ");");
                            }
                            break;
                        }
                        case 4:
                        {
                            int64_t valVE = KYFG_GetGrabberValueEnum(fgHandle, var2);
                            if (INVALID_INT_PARAMETER_VALUE == valVE)
                            {
                                PRINT_LOG( "", "Invalid int parameter value: " << std::hex << "0X" << valVE << ");" << std::dec);
                            }
                            else
                            {
                                PRINT_LOG( "", valVE << ");");
                            }
                            break;
                        }
                        case 6:
                        {


                            uint8_t *RecvBuffer = (uint8_t *)malloc(1); // Allocate 1 byte to start
                                    if (RecvBuffer == nullptr)
                                    {
                                        PRINT_LOG( "", "Memory allocation failed." << std::endl);
                                    }

                                    uint32_t RecvBufferSize = 0; // Initially set RecvLen to 1 (size of the first allocation)
                                    FGSTATUS valR = KYFG_GetGrabberValueRegister(camHandle, var2, RecvBuffer, &RecvBufferSize);

                                    if (valR == FGSTATUS_BUFFER_TOO_SMALL)
                                    {
                                        // If buffer is too small, realloc to the required size
                                        RecvBuffer = (uint8_t *)realloc(RecvBuffer, RecvBufferSize); // Resize the buffer to the new size
                                        if (RecvBuffer == nullptr)
                                        {
                                            PRINT_LOG( "", "Memory reallocation failed." << std::endl);
                                        }

                                        // Try again to get the value with the resized buffer
                                        valR = KYFG_GetGrabberValueRegister(camHandle, var2, RecvBuffer, &RecvBufferSize);
                                    }

                                    if (valR == FGSTATUS_OK)
                                    {
                                        if (RecvBufferSize == 0)
                                        {
                                            PRINT_LOG( "", "Empty String;");
                                        }
                                        else
                                        {
                                            for(int i=0;i<RecvBufferSize;i++){
                                                PRINT_LOG("", std::hex<<RecvBuffer[i]);
                                            }
                                            PRINT_LOG("", std::dec<<");");
                                            
                                        }
                                    }
                                    else
                                    {
                                        PRINT_LOG( "", "Unable to get register due to:  " << std::hex << "0X" << valR << ");" << std::dec);
                                    }

                                    // Free the dynamically allocated memory after use
                                    free(RecvBuffer);
                                    break;
                        }
                        default:
                        {
                            PRINT_LOG( "", "unrecognized Parameter Name;");
                            break;
                        }
                        }
                    }
                    else
                    { // Set Grabber
                        PRINT_LOG( "", " set " << var2 << "(" << dataTypes[type + 1] << ")= ");
                        switch (type)
                        {
                        case 0:
                            try
                            {
                                int64_t valI = std::stoll(var3);
                                FGSTATUS status = KYFG_SetGrabberValueInt(fgHandle, var2, valI);
                                if (status == FGSTATUS_OK)
                                {
                                    PRINT_LOG( "", KYFG_GetGrabberValueInt(fgHandle, var2) << ");");
                                }
                                else
                                {
                                    PRINT_LOG( "", "\nUnable to set " << var2 << " due to:  " << std::hex << "0X" << status << ");" << std::dec);
                                }
                                break;
                            }
                            catch (const std::exception &e)
                            {
                                PRINT_LOG( "", "Exception: " << e.what() << std::endl);
                            }

                        case 1:
                            try
                            {
                                bool valB;
                                if (stoi(var3) == 0)
                                    valB = false;
                                else
                                    valB = true;

                                FGSTATUS status = KYFG_SetGrabberValueBool(fgHandle, var2, valB);
                                if (FGSTATUS_OK == status)
                                {
                                    valB = KYFG_GetGrabberValueBool(fgHandle, var2);
                                    PRINT_LOG( "", valB << ");");
                                }
                                else
                                {
                                    PRINT_LOG( "", "\nUnable to set " << var2 << " due to:  " << std::hex << "0X" << status << ");" << std::dec);
                                }
                                break;
                            }
                            catch (const std::exception &e)
                            {
                                PRINT_LOG( "", "Exception: " << e.what() << std::endl);
                            }

                        case 2:
                            try
                            {
                                const char *valS = var3.c_str();

                                FGSTATUS status = KYFG_SetGrabberValueString(fgHandle, var2, valS);
                                if (FGSTATUS_OK == status)
                                {
                                    char *RecvStr = (char *)malloc(1); // Allocate 1 byte to start
                                    // char* RecvStr = nullptr;
                                    if (RecvStr == nullptr)
                                    {
                                        PRINT_LOG( "", "Memory allocation failed." << std::endl);
                                    }

                                    uint32_t RecvLen = 0; // Initially set RecvLen to 1 (size of the first allocation)
                                    FGSTATUS valS = KYFG_GetGrabberValueStringCopy(fgHandle, var2, RecvStr, &RecvLen);

                                    if (valS == FGSTATUS_BUFFER_TOO_SMALL)
                                    {
                                        // If buffer is too small, realloc to the required size
                                        RecvStr = (char *)realloc(RecvStr, RecvLen); // Resize the buffer to the new size
                                        if (RecvStr == nullptr)
                                        {
                                            PRINT_LOG( "", "Memory reallocation failed." << std::endl);
                                        }

                                        // Try again to get the value with the resized buffer
                                        valS = KYFG_GetGrabberValueStringCopy(fgHandle, var2, RecvStr, &RecvLen);
                                    }

                                    if (valS == FGSTATUS_OK)
                                    {
                                        if (RecvLen == 0)
                                        {
                                            PRINT_LOG( "", "Empty String;");
                                        }
                                        else
                                        {
                                            PRINT_LOG( "", RecvStr << ");");
                                        }
                                    }
                                    else
                                    {
                                        PRINT_LOG( "", "Unable to get String due to:  " << std::hex << "0X" << valS << ");" << std::dec);
                                    }

                                    // Free the dynamically allocated memory after use
                                    free(RecvStr);
                                }
                                else
                                {
                                    PRINT_LOG( "", "\nUnable to set " << var2 << " due to:  " << std::hex << "0X" << status << ");" << std::dec);
                                }
                                break;
                            }
                            catch (const std::exception &e)
                            {
                                PRINT_LOG( "", "Exception: " << e.what() << std::endl);
                            }

                        case 3:
                            try
                            {
                                double valF = std::stod(var3);
                                FGSTATUS status = KYFG_SetGrabberValueFloat(fgHandle, var2, valF);
                                if (status == FGSTATUS_OK)
                                {
                                    PRINT_LOG( "", KYFG_GetGrabberValueFloat(fgHandle, var2) << ");");
                                }
                                else
                                {
                                    PRINT_LOG( "", "\nUnable to set " << var2 << " due to:  " << std::hex << "0X" << status << ");" << std::dec);
                                }
                                break;
                            }
                            catch (const std::exception &e)
                            {
                                PRINT_LOG( "", "Exception: " << e.what() << std::endl);
                            }

                        case 4:
                            try
                            {
                                int64_t valVE = std::stoll(var3);
                                FGSTATUS status = KYFG_SetGrabberValueEnum(fgHandle, var2, valVE);
                                if (status == FGSTATUS_OK)
                                {
                                    PRINT_LOG( "", KYFG_GetGrabberValueEnum(fgHandle, var2) << ");");
                                }
                                else
                                {
                                    PRINT_LOG( "", "\nUnable to set " << var2 << " due to:  " << std::hex << "0X" << status << ");" << std::dec);
                                }
                                break;
                            }
                            catch (const std::exception &e)
                            {
                                PRINT_LOG( "", "Exception: " << e.what() << std::endl);
                            }

                        case 5:
                            try
                            {
                                const char *valC = var3.c_str();
                                FGSTATUS status = KYFG_GrabberExecuteCommand(fgHandle, var2); // valC not there in API
                                if (status == FGSTATUS_OK)
                                {
                                    PRINT_LOG( "", "Command Executed successfully;");
                                }
                                else
                                {
                                    PRINT_LOG( "", "\nUnable to execute command " << var2 << " due to:  " << std::hex << "0X" << status << ");" << std::dec);
                                }
                                break;
                            }
                            catch (const std::exception &e)
                            {
                                PRINT_LOG( "", "Exception: " << e.what() << std::endl);
                            }
                        default:
                        {
                            PRINT_LOG( "", "unrecognized Parameter Name;");
                            break;
                        }
                            // case 1: KYBOOL valB= KYFG_SetGrabberValueBool(fgHandle, var3); break;
                        }
                    }
                }
                else
                {                                         // Camera
                    if (var1[0] == 'c' || var1[0] == 'C') // Only check the first char from the string
                    {                                     // Get Camera
                        KY_CAM_PROPERTY_TYPE type = KYFG_GetCameraValueType(camHandle, var2);

                        PRINT_LOG( "", "Camera: ");
                        if (var3 == "?")
                        { // Getters
                            PRINT_LOG( "", " get " << var2 << "(" << dataTypes[type + 1] << ")= ");
                            switch (type)
                            {
                            case 0:
                            {
                                int64_t valI = KYFG_GetCameraValueInt(camHandle, var2);
                                if (INVALID_INT_PARAMETER_VALUE == valI)
                                {
                                    PRINT_LOG( "", "Invalid int parameter value: " << std::hex << "0X" << valI << ");" << std::dec);
                                }
                                else
                                {
                                    PRINT_LOG( "", valI << ");");
                                }
                                break;
                            }
                            case 1:
                            {
                                bool valB = KYFG_GetCameraValueBool(camHandle, var2);
                                PRINT_LOG( "", valB << ");");
                                break;
                            }
                            case 2:
                            {
                                char *RecvStr = (char *)malloc(1); // Allocate 1 byte to start
                                // char* RecvStr = nullptr;
                                if (RecvStr == nullptr)
                                {
                                    PRINT_LOG( "", "Memory allocation failed." << std::endl);
                                }

                                uint32_t RecvLen = 0; // Initially set RecvLen to 1 (size of the first allocation)
                                FGSTATUS valS = KYFG_GetCameraValueStringCopy(camHandle, var2, RecvStr, &RecvLen);

                                if (valS == FGSTATUS_BUFFER_TOO_SMALL)
                                {
                                    // If buffer is too small, realloc to the required size
                                    RecvStr = (char *)realloc(RecvStr, RecvLen); // Resize the buffer to the new size
                                    if (RecvStr == nullptr)
                                    {
                                        PRINT_LOG( "", "Memory reallocation failed." << std::endl);
                                    }

                                    // Try again to get the value with the resized buffer
                                    valS = KYFG_GetCameraValueStringCopy(camHandle, var2, RecvStr, &RecvLen);
                                }

                                if (valS == FGSTATUS_OK)
                                {
                                    if (RecvLen == 0)
                                    {
                                        PRINT_LOG( "", "Empty String;");
                                    }
                                    else
                                    {
                                        PRINT_LOG( "", RecvStr << ");");
                                    }
                                }
                                else
                                {
                                    PRINT_LOG( "", "Unable to get String due to:  " << std::hex << "0X" << valS << ");" << std::dec);
                                }

                                // Free the dynamically allocated memory after use
                                free(RecvStr);
                                break;
                            }
                            case 3:
                            {
                                double valF = KYFG_GetCameraValueFloat(camHandle, var2);
                                if (INVALID_FLOAT_PARAMETER_VALUE == valF)
                                {
                                    PRINT_LOG( "", "Invalid Float parameter value: " << std::hex << "0X" << valF << ");" << std::dec);
                                }
                                else
                                {
                                    PRINT_LOG( "", valF << ");");
                                }
                                break;
                            }
                            case 4:
                            {
                                int64_t valVE = KYFG_GetCameraValueEnum(camHandle, var2);
                                if (INVALID_INT_PARAMETER_VALUE == valVE)
                                {
                                    PRINT_LOG( "", "Invalid int parameter value: " << std::hex << "0X" << valVE << ");" << std::dec);
                                }
                                else
                                {
                                    PRINT_LOG( "", valVE << ");");
                                }
                                break;
                            }
                            case 6:
                            {
                                uint8_t *RecvBuffer = (uint8_t *)malloc(1); // Allocate 1 byte to start
                                // char* RecvStr = nullptr;
                                if (RecvBuffer == nullptr)
                                {
                                    PRINT_LOG( "", "Memory allocation failed." << std::endl);
                                }

                                uint32_t RecvBufferSize = 0; // Initially set RecvLen to 1 (size of the first allocation)
                                FGSTATUS valR = KYFG_GetCameraValueRegister(camHandle, var2, RecvBuffer, &RecvBufferSize);

                                if (valR == FGSTATUS_BUFFER_TOO_SMALL)
                                {
                                    // If buffer is too small, realloc to the required size
                                    RecvBuffer = (uint8_t *)realloc(RecvBuffer, RecvBufferSize); // Resize the buffer to the new size
                                    if (RecvBuffer == nullptr)
                                    {
                                        PRINT_LOG( "", "Memory reallocation failed." << std::endl);
                                    }

                                    // Try again to get the value with the resized buffer
                                    valR = KYFG_GetCameraValueRegister(camHandle, var2, RecvBuffer, &RecvBufferSize);
                                }

                                if (valR == FGSTATUS_OK)
                                {
                                    if (RecvBufferSize == 0)
                                    {
                                        PRINT_LOG( "", "Empty String;");
                                    }
                                    else
                                    {
                                        for(int i=0;i<RecvBufferSize;i++){
                                            PRINT_LOG("", std::hex<<RecvBuffer[i]);
                                        }
                                        PRINT_LOG("", std::dec<<");");
                                        
                                    }
                                }
                                else
                                {
                                    PRINT_LOG( "", "Unable to get register due to:  " << std::hex << "0X" << valR << ");" << std::dec);
                                }

                                // Free the dynamically allocated memory after use
                                free(RecvBuffer);
                                break;
                                // uint8_t RecvBuffer[48] = {0};
                                // uint32_t RecvBufferSize = sizeof(RecvBuffer);
                                // FGSTATUS valR = KYFG_GetCameraValueRegister(camHandle, var2, RecvBuffer, &RecvBufferSize);
                                // if (valR == FGSTATUS_OK)
                                // {
                                //     PRINT_LOG( std::hex<<RecvBuffer<<std::dec << "); to be tested"<<RecvBufferSize);
                                //     // KYFG_SetCameraValue(camHandle, "var2", RecvBuffer);
                                // }
                                // else
                                // {
                                //     PRINT_LOG( "Unable to get register due to:  " << std::hex << "0X" << valR << ");" << std::dec);
                                // }
                                // break;
                            }
                            default:
                            {
                                PRINT_LOG( "", "unrecognized Parameter Name;" << std::endl);
                                break;
                            }
                            }
                        }
                        else
                        { // Set Cam
                            PRINT_LOG( "", " set " << var2 << "(" << dataTypes[type + 1] << "):  ");
                            switch (type)
                            {
                            case 0:
                                try
                                {
                                    int64_t valI = std::stoll(var3);
                                    FGSTATUS status = KYFG_SetCameraValueInt(camHandle, var2, valI);
                                    if (status == FGSTATUS_OK)
                                    {
                                        PRINT_LOG( "", KYFG_GetCameraValueInt(camHandle, var2) << ");");
                                    }
                                    else
                                    {
                                        PRINT_LOG( "", "\nUnable to set " << var2 << " due to:  " << std::hex << "0X" << status << ");" << std::dec);
                                    }
                                    break;
                                }
                                catch (const std::exception &e)
                                {
                                    PRINT_LOG( "", "Exception: " << e.what() << std::endl);
                                }

                            case 1:
                                try
                                {
                                    bool valB;
                                    if (stoi(var3) == 0)
                                        valB = KYFALSE;
                                    else
                                        valB = KYTRUE;
                                    // PRINT_LOG("ValB="<<valB<<std::endl);
                                    // std::istringstream(var3) >> std::boolalpha >> valB;
                                    FGSTATUS status = KYFG_SetCameraValueBool(camHandle, var2, valB);
                                    if (status == FGSTATUS_OK)
                                    {
                                        bool valB = KYFG_GetCameraValueBool(camHandle, var2);
                                        PRINT_LOG( "", valB << ");");
                                    }
                                    else
                                    {
                                        PRINT_LOG( "", "\nUnable to set " << var2 << " due to:  " << std::hex << "0X" << status << ");" << std::dec);
                                    }
                                    break;
                                }
                                catch (const std::exception &e)
                                {
                                    PRINT_LOG( "", "Exception: " << e.what() << std::endl);
                                }

                            case 2:
                                try
                                {
                                    const char *valS = var3.c_str();
                                    FGSTATUS status = KYFG_SetCameraValueString(camHandle, var2, valS);
                                    if (FGSTATUS_OK == status)
                                    {
                                        char *RecvStr = (char *)malloc(1); // Allocate 1 byte to start
                                        if (RecvStr == nullptr)
                                        {
                                            PRINT_LOG( "", "Memory allocation failed." << std::endl);
                                        }

                                        uint32_t RecvLen = 0; // Initially set RecvLen to 1 (size of the first allocation)
                                        FGSTATUS valS = KYFG_GetCameraValueStringCopy(camHandle, var2, RecvStr, &RecvLen);

                                        if (valS == FGSTATUS_BUFFER_TOO_SMALL)
                                        {
                                            // If buffer is too small, realloc to the required size
                                            RecvStr = (char *)realloc(RecvStr, RecvLen); // Resize the buffer to the new size
                                            if (RecvStr == nullptr)
                                            {
                                                PRINT_LOG( "", "Memory reallocation failed." << std::endl);
                                            }

                                            // Try again to get the value with the resized buffer
                                            valS = KYFG_GetCameraValueStringCopy(camHandle, var2, RecvStr, &RecvLen);
                                        }

                                        if (valS == FGSTATUS_OK)
                                        {
                                            if (RecvLen == 0)
                                            {
                                                PRINT_LOG( "", "Empty String;");
                                            }
                                            else
                                            {
                                                PRINT_LOG( "", RecvStr << ");");
                                            }
                                        }
                                        else
                                        {
                                            PRINT_LOG( "", "Unable to get String due to:  " << std::hex << "0X" << valS << ");" << std::dec);
                                        }

                                        // Free the dynamically allocated memory after use
                                        free(RecvStr);
                                    }
                                    else
                                    {
                                        PRINT_LOG( "", "\nUnable to set " << var2 << " due to:  " << std::hex << "0X" << status << ");" << std::dec);
                                    }
                                    break;
                                }
                                catch (const std::exception &e)
                                {
                                    PRINT_LOG( "", "Exception: " << e.what() << std::endl);
                                }

                            case 3:
                                try
                                {
                                    double valF = std::stod(var3);
                                    FGSTATUS status = KYFG_SetCameraValueFloat(camHandle, var2, valF);
                                    if (status == FGSTATUS_OK)
                                    {
                                        PRINT_LOG( "", KYFG_GetCameraValueFloat(camHandle, var2) << ");");
                                    }
                                    else
                                    {
                                        PRINT_LOG( "", "\nUnable to set " << var2 << " due to:  " << std::hex << "0X" << status << ");" << std::dec);
                                    }
                                    break;
                                }
                                catch (const std::exception &e)
                                {
                                    PRINT_LOG( "", "Exception: " << e.what() << std::endl);
                                }

                            case 4:
                                try
                                {
                                    int64_t valVE = std::stoll(var3);
                                    FGSTATUS status = KYFG_SetCameraValueEnum(camHandle, var2, valVE);
                                    if (status == FGSTATUS_OK)
                                    {
                                        PRINT_LOG( "", KYFG_GetCameraValueEnum(camHandle, var2) << ");");
                                    }
                                    else
                                    {
                                        PRINT_LOG( "", "\nUnable to set " << var2 << " due to:  " << std::hex << "0X" << status << ");" << std::dec);
                                    }
                                    break;
                                }
                                catch (const std::exception &e)
                                {
                                    PRINT_LOG( "", "Exception: " << e.what() << std::endl);
                                }

                            case 5:
                                try
                                {
                                    const char *valC = var3.c_str();
                                    PRINT_LOG( "", valC);
                                    FGSTATUS status = KYFG_CameraExecuteCommand(camHandle, var2); // valC not there in API
                                    if (status == FGSTATUS_OK)
                                    {
                                        PRINT_LOG( "", "Command Executed successfully;");
                                    }
                                    else
                                    {
                                        PRINT_LOG( "", "\nUnable to execute command " << var2 << " due to:  " << std::hex << "0X" << status << ");" << std::dec);
                                    }
                                    break;
                                }
                                catch (const std::exception &e)
                                {
                                    PRINT_LOG( "", "Exception: " << e.what() << std::endl);
                                } // KYFG_SetCameraValueRegister() Require clarification
                            case 6:
                                try{
                                    const char *valS = var3.c_str();
                                    FGSTATUS status =  KYFG_SetCameraValue(camHandle, var2, (void*)valS);
                                    if(status == FGSTATUS_OK)
                                    {
                                        PRINT_LOG("", "Buffer set successfully;");
                                    }
                                    else{
                                        PRINT_LOG( "", "\nUnable to set " << var2 << " due to:  " << std::hex << "0X" << status << ");" << std::dec);
                                    }
                                    break;
                                }   
                                catch (const std::exception &e)
                                {
                                    PRINT_LOG( "", "Exception: " << e.what() << std::endl);
                                } 
                            default:
                            {
                                PRINT_LOG( "", "unrecognized Parameter Name;");
                                break;
                            }
                            }
                        }
                    }
                }
                //PRINT_LOG( std::endl;
            }
            else
            {
                PRINT_LOG( "", "Skipping an item " << item["VariableName"] << " due to missing data: " << std::endl);
                if (!item.contains("Device"))
                    PRINT_LOG( "", "Missing 'Device' " << std::endl);
                if (!item.contains("VariableName"))
                    PRINT_LOG( "", "Missing 'VariableName' " << std::endl);
                if (!item.contains("Value"))
                    PRINT_LOG( "", "Missing 'Value' " << std::endl);
            }
        }
    }
    return 0;
}

bool Camera::Close(int CameraID)
{
    PRINT_LOG("", "Closing the Camera ["<<CameraID<<"]...\n");
    if (this->camConnected[CameraID] == KYTRUE)
    {
        if (FGSTATUS_OK == KYFG_CameraClose(this->camHandles[CameraID]))
        {
            this->camConnected[CameraID] = KYFALSE;
            (this->nConnected) -= 1;
            PRINT_LOG("", "done\n");
            return true;
        }
        else{
            PRINT_LOG("", "unsuccessful\n");
        }
    }
    else{
        PRINT_LOG("", "not open\n");
    }
    return (false);
}

int Camera::CloseAll(void)
{
    int ClosedCount = 0;
    for (int CameraID = 0; CameraID < this->nDetected; CameraID++) // All the cameras
    {
        PRINT_LOG("[I75]", "Closing the Camera ["<<CameraID<<"]...\n");
        if (this->camConnected[CameraID] == KYTRUE)
        {
            if (FGSTATUS_OK == KYFG_CameraClose(this->camHandles[CameraID]))
            {
                this->camConnected[CameraID] = KYFALSE;
                (this->nConnected) -= 1;
                ClosedCount++;
                PRINT_LOG("[I76]", "Successfully closed the camera.\n");
            }
            else{
                PRINT_LOG("[E41]", "Failed to close camera\n");
            }
        }
        else{
            PRINT_LOG("[I77]", "Camera not open\n");
        }
    }
    return (ClosedCount);
}


