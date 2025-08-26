#include "GenericParam.h"

int64_t dmaImageIdCapable = 0;

unsigned int currentGrabberIndex;

int printCxp2Events = 0;
int printHeartbeats = 0;

void ProcessHeartbeatEvent(KYDEVICE_EVENT_CXP2_HEARTBEAT* pEventHeartbeat)
{
    if (!printHeartbeats)
    {
        return;
    }
   
    PRINT_LOG("", "Received KYDEVICE_EVENT_CXP2_HEARTBEAT: cameraTime=" << pEventHeartbeat->heartBeat.cameraTime << "\n");

}

void ProcessCxp2Event(KYDEVICE_EVENT_CXP2_EVENT* pEventCXP2Event)
{
    if (!printCxp2Events)
    {
        return;
    }
   
    PRINT_LOG("", "Received KYDEVICE_EVENT_CXP2_EVENT: tag=0x" << std::hex << std::uppercase << static_cast<int>(pEventCXP2Event->cxp2Event.tag) << std::dec << "\n");
}


void KYDeviceEventCallBackImpl(void* userContext, KYDEVICE_EVENT* pEvent)
{
    userContext; // suppress warning
    switch (pEvent->eventId)
    {
        // Please note that the following events will be recieved only if camera is working in CXP 2 mode. For details please reffer CXP 2 standards
        case KYDEVICE_EVENT_CXP2_HEARTBEAT_ID:
            ProcessHeartbeatEvent((KYDEVICE_EVENT_CXP2_HEARTBEAT*)pEvent);
            break;
        case KYDEVICE_EVENT_CXP2_EVENT_ID:
            ProcessCxp2Event((KYDEVICE_EVENT_CXP2_EVENT*)pEvent);
            break;
    }
}


//System can have multiple Grabber board but, only one at a time to be active.
class Grabber{
	private:
        FGHANDLE Handle;
        KY_DEVICE_INFO *Infos= NULL;
        FGSTATUS *flags= NULL;
		int n;//Total= Physical + Virtual
		int ID;//The ID from the available Grabber connected
		std::string Name;//= "KAYA Dev Device 0xD001";

	public:
		Grabber();
        ~Grabber();
		int  TotalAvailable(void);
		void DisplayList(void);
		bool ConnectByID(int ID);
        bool AutoConnect(void);
		int  SearchByDisplayName(string Name);
		bool ConnectByName(string Name);
        FGHANDLE GetHandle();
        int GetID();
        string GetName();
  		bool  Close();      
        void getHardwareInfo(FGHANDLE fgHandle);
};

Grabber:: Grabber()
{
	this->Handle = INVALID_FGHANDLE;
	this->ID = 0;//Assign the First grabber by default
	this->Name= "KAYA Dev Device 0xD001";
    KY_DeviceScan(&(this->n));	// First scan for device to retrieve the number of virtual and hardware devices connected to PC
    Infos= new KY_DEVICE_INFO [this->n];
    flags= new FGSTATUS [this->n];
    memset(Infos, 0, this->n * sizeof(KY_DEVICE_INFO));
    for(int i=0; i < this->n && i<KY_MAX_BOARDS; i++)
    {
        this->Infos[i].version = KY_MAX_DEVICE_INFO_VERSION;
        this->flags[i]= KY_DeviceInfo(i, &(this->Infos[i]));
    }
}

 Grabber:: ~Grabber()
{
    delete [] this->Infos;
}

int Grabber::TotalAvailable(){
	return (this->n);
}

void  Grabber::DisplayList()
{
   
    PRINT_LOG("[I04]", "Number of KAYA PCI devices found: " << this->n<<std::endl);
    fflush(stdout);
	for(int i=0; i < this->n && i<KY_MAX_BOARDS; i++)
    {
        if (FGSTATUS_OK != this->flags[i])
        {
           
            PRINT_LOG("[E02]", "Grabber[" << i << "]: Unable to retrive information "<< std::endl);
            fflush(stdout);
            continue;
        }
       
        PRINT_LOG("[I05]", "Grabber[" << i << "]: " << this->Infos[i].szDeviceDisplayName
          << " on PCI slot {" << this->Infos[i].nBus << ":" << this->Infos[i].nSlot << ":" << this->Infos[i].nFunction
          << "}: Protocol 0x" << std::hex << std::uppercase << this->Infos[i].m_Protocol
          << std::dec << ", Generation " << this->Infos[i].DeviceGeneration << "\n");
        fflush(stdout);
    }
}


bool Grabber::ConnectByID(int grabberIndex)
{
    if(!(grabberIndex>=0 && grabberIndex<this->n)){
       
        PRINT_LOG("", "Out of Range ID: " << grabberIndex << "\n");
    }

    if (FGSTATUS_OK != this->flags[grabberIndex])//KY_DeviceInfo(grabberIndex, &(this->Infos[grabberIndex])))
    {
       
        PRINT_LOG("", "wasn't able to retrive information from device #" << grabberIndex << "\n");
        return false;
    }
    else{
       
        PRINT_LOG("", "Grabber[" << grabberIndex << "]: " << this->Infos[grabberIndex].szDeviceDisplayName
          << " on PCI slot {" << this->Infos[grabberIndex].nBus << ":" << this->Infos[grabberIndex].nSlot << ":" << this->Infos[grabberIndex].nFunction
          << "}: Protocol 0x" << std::hex << std::uppercase << this->Infos[grabberIndex].m_Protocol
          << std::dec << ", Generation " << this->Infos[grabberIndex].DeviceGeneration << "\n");
    }

    if (0 == (KY_DEVICE_STREAM_GRABBER & this->Infos[grabberIndex].m_Flags))
    {
       
        PRINT_LOG("", "Selected device #" << grabberIndex << " is not a grabber\n");
        return false;
    }

    // this->Handle = KYFG_OpenEx(grabberIndex, NULL);//File Path to be Added
    this->Handle = KYFG_Open(grabberIndex);
    if (this->Handle != INVALID_FGHANDLE){
       
        PRINT_LOG("", "Good connection to grabber #" << grabberIndex << ", FgHandles=0x" << std::hex << std::uppercase << this->Handle << std::dec << "\n");
        latestTelemetry.cameraInfo.cameraStatus = 1;
        latestTelemetry.cameraInfo.GrabberStatus = 2; // Connected
    }
    else{
       
        PRINT_LOG("", "Could not connect to grabber #" << grabberIndex << "\n");
        return false;
    }
                
    // dmaQueuedBufferCapable;
    int64_t dmaQueuedBufferCapable = KYFG_GetGrabberValueInt(this->Handle, DEVICE_QUEUED_BUFFERS_SUPPORTED);
    if (1 != dmaQueuedBufferCapable)
    {
       
        PRINT_LOG("", "grabber #" << grabberIndex << " does not support queued buffers\n");
        return false;
    }


    dmaImageIdCapable = KYFG_GetGrabberValueInt(this->Handle, DEVICE_IMAGEID_SUPPORTED);
   
    PRINT_LOG("", "grabber #" << grabberIndex << ", KY_STREAM_BUFFER_INFO_IMAGEID " << (dmaImageIdCapable ? "" : "not ") << "supported\n");
    
    // OPTIONALY register grabber's event callback function
   
    if (FGSTATUS_OK != KYDeviceEventCallBackRegister(this->Handle, KYDeviceEventCallBackImpl, 0))
        PRINT_LOG("", "Warning: KYDeviceEventCallBackRegister() failed\n");
    else
        PRINT_LOG("", "KYDeviceEventCallBackImpl() registered \n");//, enter 'v' to turn event prints on and off\n");
        
    //Grabber is ok to continue
    this->ID= grabberIndex;
    this->Name= this->Infos[grabberIndex].szDeviceDisplayName;
    
    return true;
}


bool Grabber::AutoConnect(void)
{
   
    PRINT_LOG("[I06]", "Autoconnect Initiated..."<<endl);
    for(int i=0; i<this->n && i<KY_MAX_BOARDS; i++)
    {
        if(this->Infos[i].isVirtual== false)//try only physical port
        {
            bool flag= ConnectByID(i);
            if(flag== true)
                return true;
        }    
    }
   
    PRINT_LOG("[E03]", "Auto Connect Failed..."<<endl);
    // std::this_thread::sleep_for(std::chrono::milliseconds(15000)); // Wait for 15 seconds
    return false;
}

void Grabber::getHardwareInfo(FGHANDLE fgHandle){
   
    PRINT_LOG("", "HardWare Information: \n");

    // Getting value of 'DeviceFirmwareVersion'
    // 1. get required string size:
    uint32_t stringSize = 0;
    KYFG_GetGrabberValueStringCopy(fgHandle, "DeviceFirmwareVersion", nullptr, &stringSize);
    // 2. allocate memory for string:
    char* pvalue_str = (char*)malloc(stringSize); // allocate memory for string
    // 3. retrieve copy of string:
    KYFG_GetGrabberValueStringCopy(fgHandle, "DeviceFirmwareVersion", pvalue_str, &stringSize);
    PRINT_LOG("", "Device Firmware Version: "<<pvalue_str<<endl);

    int coreTemp = KYFG_GetGrabberValueInt(fgHandle, "DeviceTemperature");
    PRINT_LOG("", "Device Core Temperature: "<<coreTemp<<endl);
    int PciGen = KYFG_GetGrabberValueInt(fgHandle, "DevicePciGeneration");
    PRINT_LOG("", "Detected Device Pci Generation: "<<PciGen<<endl);
   
    int PciLanes = KYFG_GetGrabberValueInt(fgHandle, "DevicePciLanes");
    PRINT_LOG("", "Detected Device Pci Lanes: "<<PciLanes<<endl);

}   

int  Grabber::SearchByDisplayName(string GrabberName)
{
	for(int i=0; i < this->n; i++)
    {
            if(GrabberName.compare(0, GrabberName.length(), this->Infos[i].szDeviceDisplayName, 0, GrabberName.length())==0)
            {
               
                PRINT_LOG("", "Grabber["<<i<<"] display name found as: "<< GrabberName <<std::endl);
                return(i);//Assign the PCIe Grabber 
            }
     }
	 return(-1);
}

// Helper function to get single printable char as user input
int get_printable_char()
{
    int c;
    fflush(stdin);
    do
        c = getchar();
    while (isspace(c));
    return c;
}

FGHANDLE Grabber:: GetHandle()
{
    return (this->Handle);
}

int Grabber:: GetID()
{
    return (this->ID);
}

string Grabber:: GetName()
{
    return (this->Name);
}

bool  Grabber::ConnectByName(string Name)
{
   
    PRINT_LOG("", "ConnectByName..."<<endl);
    int grabberID= SearchByDisplayName(Name);
    bool flag= ConnectByID(grabberID);
    return flag;
}

bool Grabber::Close()
{
   
    PRINT_LOG("[I78]", "Closing the Grabber...\n");
   
    if(INVALID_FGHANDLE != this->Handle)
    {
        if(FGSTATUS_OK == KYFG_Close(this->Handle)) // Close the  device and unregisters all associated routines
        {
            PRINT_LOG("[I79]", "Grabber closed successfully\n");
            return true;
        }
    }
    PRINT_LOG("[E42]", "Failed to close Grabber."<<std::endl);
    return false;
}

