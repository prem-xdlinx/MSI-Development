#ifndef XDLX_CAMERA_H
#define XDLX_CAMERA_H


class Camera{
	private:
        CAMHANDLE camHandles[KY_MAX_CAMERAS];        // there are maximum KY_MAX_CAMERAS cameras
        //KYFGCAMERA_INFO2* cameraInfoArray = NULL;
        KYFGCAMERA_INFO2* Infos = NULL;
		KYBOOL cameraConnected[KY_MAX_CAMERAS];//delete

		int nDetected= 0;
        int nConnected= 0;
		//int Total;//=0;
		//int ID ;//=1;//Assign the First grabber by default
		//std::string Name;//= "KAYA Dev Device 0xD001";
        //char CamSettingsZipPath[Max_File_Path_Len]= "/opt/KAYA_Instruments/Examples/Vision Point API/XDLX_KYFG_V1/ConfigData/camera.zip";
        //char CamSettingsXmlPath[Max_File_Path_Len]= "/opt/KAYA_Instruments/Examples/Vision Point API/XDLX_KYFG_V1/ConfigData/camera.xml";

	public:
		Camera();
		//Camera(int ID, std::string Name);
        int Detect(FGHANDLE fgHandle);//(Grabber CurrentGrabber);
		int Connect(FGHANDLE fgHandle, int CameraID);
		int SettingsDefault(FGHANDLE fgHandle, CAMHANDLE camHandle);
		int SettingsJSON(FGHANDLE fgHandle, CAMHANDLE camHandle);
		int SettingUserInput(FGHANDLE fgHandle, CAMHANDLE camHandle);
		int getSettingsApplied(FGHANDLE fgHandle, CAMHANDLE camHandle);
		int ConnectDesired(FGHANDLE fgHandle, unsigned char CamMask);
		int ConnectDesiredRegion(CAMHANDLE camHandle, unsigned char CamMask);
		KYBOOL IsConnected(int CameraID);
		CAMHANDLE Handle(int CameraID);
		//int SettingsAll(Grabber CurrentGrabber, int SettingsID);
		//int Settings(Grabber CurrentGrabber, FILE *FileID, int CameraID);
		//int SettingsTest(FGHANDLE fgHandle);//Testing Purpose
		void Display(KYFGCAMERA_INFO2 *cameraInfo);
		void ReadSettings(void);
		int StartCamera(unsigned int grabberIndex, int CamID, STREAM_HANDLE cameraStreamHandle, unsigned int FramesN);
        bool Close(int CameraID);
        int CloseAll();

        //SetParameterFromFile(int grabberID, int CameraID, FILE *FileID);
        //FactoryDefault(int grabberID, int CameraID, FILE *FileID);

		/*int TotalAvailable(void);
		void DisplayList(void);
		int  SearchByDisplayName(string Name);
		bool  ConnectByID(int ID);
		bool  ConnectByName(string Name);
  		bool  Close();   */  
		bool StopAll();
 
};

#endif