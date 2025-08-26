#ifndef XDLX_KAYA_GRABBER_H
#define XDLX_KAYA_GRABBER_H




void ProcessHeartbeatEvent(KYDEVICE_EVENT_CXP2_HEARTBEAT* pEventHeartbeat);
void ProcessCxp2Event(KYDEVICE_EVENT_CXP2_EVENT* pEventCXP2Event);
void KYDeviceEventCallBackImpl(void* userContext, KYDEVICE_EVENT* pEvent);

class Grabber{
	private:
		FGHANDLE Handles[KY_MAX_BOARDS];
        FGHANDLE Handle;
        KY_DEVICE_INFO*  Infos;//=  new KY_DEVICE_INFO (KY_MAX_BOARDS);
		int n;//Total= Physical + Virtual
		int ID;//The ID from the available Grabber connected
		std::string Name;//= "KAYA Dev Device 0xD001";
        KY_DEVICE_INFO*  Info;//=  new KY_DEVICE_INFO;

	public:
		Grabber();
        ~Grabber();
		int TotalAvailable(void);
		void DisplayList(void);
		bool AutoConnect(void);
		int  SearchByDisplayName(std::string Name);
		bool  ConnectByID(int ID);
		bool  ConnectByName(std::string Name);
        FGHANDLE GetHandle();
        int GetID();
        std::string GetName();
  		bool  Close();      
		void getHardwareInfo(FGHANDLE fgHandle);
};



#endif