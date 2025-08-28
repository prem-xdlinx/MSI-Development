#include "GenericParam.h"

//User Input
int G_TotArg=0;
int G_OrbitNo = 0;
int G_Task=0;
unsigned char G_JSON=255;
std::tm G_Time={0}; 
int G_msec=0;
float G_Duration=4.0;//seconds
unsigned char G_Band=127;//All Bands
unsigned char G_TDI=0;//No TDI
float G_FPS=13.114;
float G_ExpTime=290.0;
float G_Gain= 1.0;
int G_BandXShift=0;
// int G_BandYShift=0;
unsigned char G_Bin = 247; // Default value for G_Bin, can be modified by user input
bool G_CCSDSProcessStatus = 1;
unsigned char G_Binning = 119; // Default value for G_Binning, can be modified by user input
int G_TDIYShift = 384;


//Derived Global Varriable
unsigned char G_TDI_Stage=0;
unsigned char G_TDI_Modes=0;
char ArguementsProcessed[MAX_PARAM_LEN];
std::string  parameter;
bool NumberInRangeInput(int min, int max, int* value, const char* error_str)
{
	bool valid = *value >= min && *value <= max;
	if(!valid)
	{
		PRINT_LOG("[E]", error_str);
	}
	return(valid);
}

std::vector<std::string> split(const std::string& s, char delimiter)
{
   std::vector<std::string> tokens;
   std::string token;
   std::istringstream tokenStream(s);
   while (std::getline(tokenStream, token, delimiter))
      tokens.push_back(token);

   return tokens;
}

int PackDateTime(const std::string date, const std::string time, std::tm *TimeNow)
{
	auto d_parts = split(date, '.');
    if (d_parts.size() != 3)
        return 0;
	TimeNow->tm_mday= std::stoi(d_parts[0]);
	TimeNow->tm_mon=  std::stoi(d_parts[1]);//-1;
	TimeNow->tm_year= std::stoi(d_parts[2]);//-1900;

	auto t_parts = split(time, ':');
    if (t_parts.size() != 3)
        return 0;
	TimeNow->tm_hour= std::stoi(t_parts[0]);
	TimeNow->tm_min=  std::stoi(t_parts[1]);
	float msec= std::stof(t_parts[2]);
	TimeNow->tm_sec=  (int) msec;
	G_msec= (int)((msec- (float)TimeNow->tm_sec)*1000);
	// TimeNow->tm_msec=  std::stoi(t_parts[3]);

	return 1;
}

int64_t TimeDifference(std::chrono::system_clock::time_point cc, bool PrintFlag)
{
	std::chrono::duration<float> difference;
	int64_t seconds=0;

	std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
	if(cc>now)
	{
		difference = cc - now;
		seconds = difference.count();
		std::time_t event_tm = std::chrono::system_clock::to_time_t(cc);// convert back to std::time_t
		std::time_t now_tm = std::chrono::system_clock::to_time_t(now);// convert back to std::time_t
		if(PrintFlag) 
		{
			PRINT_LOG("", "UTC Trigger Time: ");
			PRINT_LOG("", std::put_time(std::localtime(&event_tm), "%F %T") << std::endl);
			PRINT_LOG("", "System Time Now: ");
			PRINT_LOG("", std::put_time(std::localtime(&now_tm), "%F %T") << std::endl);
			PRINT_LOG("", "Waiting Time= "<<seconds<<" sec"<<std::endl);
		}
		return seconds;
	}
	else
		return -1;
}


int64_t TimeDifferenceUpdated(std::tm *InTime, bool PrintFlag)
{
    int64_t diffMsec = 0;
    std::chrono::system_clock::time_point cc;
    std::tm TrigTime{};

    TrigTime.tm_mday = InTime->tm_mday;
    TrigTime.tm_mon  = InTime->tm_mon - 1; // tm_mon is 0-based, so we subtract 1
    TrigTime.tm_year = InTime->tm_year - 1900; // tm_year is years since 1900
    TrigTime.tm_hour = InTime->tm_hour;
    TrigTime.tm_min  = InTime->tm_min;
    TrigTime.tm_sec  = InTime->tm_sec;

    std::time_t n = std::mktime(&TrigTime); // Ensure InTime is in local time
    cc = std::chrono::system_clock::from_time_t(n) + std::chrono::milliseconds(G_msec);
    auto usrMsecEpoc = std::chrono::duration_cast<std::chrono::milliseconds>(cc.time_since_epoch()).count();

    auto now = std::chrono::system_clock::now();
    auto sysMsecEpoc = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    diffMsec = usrMsecEpoc - sysMsecEpoc;

    if (PrintFlag) 
    {
        std::time_t event_tm = std::chrono::system_clock::to_time_t(cc);
        std::time_t now_tm   = std::chrono::system_clock::to_time_t(now);

        PRINT_LOG("[I25]", "UTC Trigger Time: ");
        PRINT_LOG("", std::put_time(std::localtime(&event_tm), "%F %T.") << G_msec << std::endl);

        PRINT_LOG("[I26]", "System Time Now: ");
        PRINT_LOG("", std::put_time(std::localtime(&now_tm), "%F %T.") << (sysMsecEpoc % 1000) << std::endl);

        PRINT_LOG("[I27]", "Waiting Time = " << diffMsec << " msec...\n");
        fflush(stdout);
    }

    return diffMsec;
}





class UI{
	private:
		string ParamFilePath;
		KY_SOFTWARE_VERSION pVersion;

	public:
		UI();
		UI(string FilePath);
		void WelcomeMessage();
		void Display( std::string  name);
		void PrintArgument(int& argc, char**& argv);
		void ReadUIFromFile(std::string  *parameter);
		void convertUI2Arg(std::string parameter, int& argc, char**& argv);
		bool Input_Validation_DefaultSetting();
		void ProcessInput( int& argc, char**& argv, MetaSet &MetaConfig);
		bool readInput(int& argc, char**& argv);
};

UI::UI()// : ParamFilePath("ConfigData/param.txt") 
{
	WelcomeMessage();
	ParamFilePath= userDirectory + "ConfigData/param.txt"; 
}

UI::UI(string FilePath) {
	WelcomeMessage();
	ParamFilePath= FilePath;
}


void UI::WelcomeMessage()
{
	
	PRINT_LOG("[I02]", "Welcome to Xdlinx Cam App Version-6.0"<<endl); 

}

void UI::Display(std::string  name)
{
	PRINT_LOG("", name<<std::endl);
}

void UI::ReadUIFromFile(std::string *parameter)
{
	std::fstream ParamFile;
	ParamFile.open(ParamFilePath, std::ios::in);
	if (!ParamFile.is_open()){
		
		PRINT_LOG("[E4]", "Unable to open ParamFile: " << ParamFilePath<<std::endl);
	}
	else{
		getline(ParamFile, *parameter, '\n');
		std::string  Param_Remain;
		getline(ParamFile, Param_Remain, '$');
		ParamFile.close(); 
		ParamFile.open(ParamFilePath, std::ios::out);
		ParamFile << Param_Remain;
		ParamFile.close();
	}
}

/*
void UI::readInput(int& argc, char**& argv){
	// ReadUIFromFile(&parameter);//Read Input From File
	int64_t msec=0;
	if(argc==1){
		ReadUIFromFile(&parameter);//Read Input From File
		PRINT_LOG("[I07] Arguments received from parameter file: "<<parameter<<endl);
		convertUI2Arg(parameter, argc, argv);//Convert String to Argument
	}
	G_TotArg= argc;	
	if(G_TotArg>4) PackDateTime(argv[4], argv[5], &G_Time);
	msec= TimeDifferenceUpdated(&G_Time, 1);
	if((msec<= MIN_TIME_TH_MSEC ) || (msec> MAX_TIME_TH_MSEC ))
	{
		PRINT_LOG("[E05] Invalid Time Difference: "<<msec<<" msec. It should be between "<<MIN_TIME_TH_MSEC<<" and "<<MAX_TIME_TH_MSEC<<".\n");
		exit(1);
	}

}
*/
bool UI::readInput(int& argc, char**& argv){
	// ReadUIFromFile(&parameter);//Read Input From File
	int64_t msec=0;
	if(argc==1){
		ReadUIFromFile(&parameter);//Read Input From File
		PRINT_LOG("[I07]", "Arguments received from parameter file: "<<parameter<<endl);
		convertUI2Arg(parameter, argc, argv);//Convert String to Argument
	}
	G_TotArg= argc;
	if(G_TotArg>0) G_OrbitNo = atoi(argv[1]);
	std::cout<<"G_OrbitNo: "<<G_OrbitNo<<std::endl;
	if(G_TotArg>1) G_Task= atoi(argv[2]);
	std::cout<<"G_Task: "<<G_Task<<std::endl;
	if(G_TotArg>4) PackDateTime(argv[4], argv[5], &G_Time);
	msec= TimeDifferenceUpdated(&G_Time, 1);
	// msec = 7000; // For testing purposes, we set msec to 7
	if((msec<= MIN_TIME_TH_MSEC ) || (msec> MAX_TIME_TH_MSEC ))
	{
		PRINT_LOG("[E]", "Invalid Time Difference: "<<msec<<" msec. It should be between "<<MIN_TIME_TH_MSEC<<" and "<<MAX_TIME_TH_MSEC<<".\n");
		return false;
	}
	return true;
}
void UI::ProcessInput(int& argc, char**& argv, MetaSet &MetaConfig){
	bool valid;
	// if(argc==1){
	// 	// ReadUIFromFile(&parameter);//Read Input From File
	// 	// PRINT_LOG("Arguments received from parameter file: "<<parameter);
	// 	convertUI2Arg(parameter, argc, argv);//Convert String to Argument
	// }
	// G_TotArg= argc;	
	// if(G_TotArg>0) G_OrbitNo = atoi(argv[1]);
	// if(G_TotArg>1) G_Task= atoi(argv[2]);
	if(G_TotArg>2) G_JSON= atoi(argv[3]);
	// if(G_TotArg>4) PackDateTime(argv[4], argv[5], &G_Time);
	if(G_TotArg>5)
	{
		 if(atof(argv[6]) != 0.0){
			G_Duration=atof(argv[6]);
		 }
	}
	if(G_TotArg>6) G_Band= (unsigned char)(atoi(argv[7]));//All Bands
	if(G_TotArg>7) G_TDI= (unsigned char)(atoi(argv[8]));//No TDI
	if(G_TotArg>8)
	{
		 if(atof(argv[9]) != 0.0){
			G_FPS=atof(argv[9]);
		 }
	}
	if(G_TotArg>9){
		if(atof(argv[10])!= 0.0){
			G_ExpTime=atof(argv[10]);
		 }
	}
	if(G_TotArg>10){
		if(atof(argv[11])!= 0.0){
			G_Gain=atof(argv[11]);
		 }
	}
	if(G_TotArg>11) G_BandXShift = atoi(argv[12]);
	// if(G_TotArg>12) G_BandYShift = atoi(argv[13]);
	if(G_TotArg>12) G_Bin = (unsigned char)atoi(argv[13]);
	// if(G_TotArg>14) G_ReadNlines = atoi(argv[15]);
#ifdef TDIYSHIFT
	if(G_TotArg>13) G_TDIYShift = atoi(argv[14]);
#endif
	
	// G_TDI_Stage= G_TDI>>2;
	// G_TDI_Modes= G_TDI & 0x03;

	// G_CCSDSProcessStatus = (G_Bin & 0x80) != 0; // Check if the 8th bit is set for CCSDS processing
	// G_Binning = G_Bin & 0x7F; // Ensure G_Binning is within 0-127 range

	// PRINT_LOG( "\nArgument Received [" << G_TotArg << "]: "
    //       << std::setw(6) << std::setfill('0') << G_OrbitNo << " "
    //       << std::setw(6) << std::setfill('0') << G_Task << " "
    //       << std::setw(3) << std::setfill('0') << static_cast<int>(G_JSON) << " "
    //       << std::setw(2) << std::setfill('0') << G_Time.tm_mday << "."
    //       << std::setw(2) << std::setfill('0') << (G_Time.tm_mon + 1) << "."  // tm_mon is 0-indexed, so add 1
    //       << std::setw(4) << std::setfill('0') << (G_Time.tm_year + 1900) << " "
    //       << std::setw(2) << std::setfill('0') << G_Time.tm_hour << ":"
    //       << std::setw(2) << std::setfill('0') << G_Time.tm_min << ":"
    //       << std::setw(2) << std::setfill('0') << G_Time.tm_sec << "."
    //       << std::fixed << std::setprecision(1) << G_msec << " "
    //       << std::fixed << std::setprecision(6) << G_Duration << " "
    //       << (int)G_Band << " "
    //       << (int)G_TDI << " "
    //       << std::fixed << std::setprecision(6) << G_FPS << " "
    //       << std::fixed << std::setprecision(6) << G_ExpTime << " "
    //       << std::fixed << std::setprecision(6) << G_Gain << " "
    //       << G_BandXShift << " "
    //       << G_BandYShift << " "
    //       << G_Binning;// << " "
          //   << G_ReadNlines);

	valid= Input_Validation_DefaultSetting();

	G_TDI_Stage= G_TDI>>2;
	G_TDI_Modes= G_TDI & 0x03;

	G_CCSDSProcessStatus = (G_Bin & 0x80) != 0; // Check if the 8th bit is set for CCSDS processing
	G_Binning = G_Bin & 0x7F; // Ensure G_Binning is within 0-127 range

#ifdef TDIYSHIFT
	snprintf(ArguementsProcessed, MAX_PARAM_LEN, "%06d %06d %03d %02d.%02d.%04d %02d:%02d:%02d.%d %f %d %d %f %f %f %d %d %d", // %d", 
	 G_OrbitNo, G_Task, G_JSON, G_Time.tm_mday, G_Time.tm_mon, G_Time.tm_year, G_Time.tm_hour, G_Time.tm_min, G_Time.tm_sec, G_msec, 
	G_Duration, G_Band, G_TDI, G_FPS, G_ExpTime, G_Gain, G_BandXShift, G_Bin, G_TDIYShift);//, G_ReadNlines);
#else
	snprintf(ArguementsProcessed, MAX_PARAM_LEN, "%06d %06d %03d %02d.%02d.%04d %02d:%02d:%02d.%d %f %d %d %f %f %f %d %d", // %d", 
	 G_OrbitNo, G_Task, G_JSON, G_Time.tm_mday, G_Time.tm_mon, G_Time.tm_year, G_Time.tm_hour, G_Time.tm_min, G_Time.tm_sec, G_msec, 
	G_Duration, G_Band, G_TDI, G_FPS, G_ExpTime, G_Gain, G_BandXShift, G_Bin);//, G_ReadNlines);
#endif
	PRINT_LOG( "[I28]", "Argument Processed[" << G_TotArg << "]: " << ArguementsProcessed << "\n");
	
	uint32_t startTimeMsec = (((G_Time.tm_hour*60*60)+ (G_Time.tm_min*60)+ G_Time.tm_sec) *1000)+G_msec;
    memcpy(MetaConfig.XDLX, "XDLX", 4);
	memcpy(MetaConfig.SAT_ID, "DHR1", 4);
	MetaConfig.OrbitNumber=G_OrbitNo;
	MetaConfig.Task_ID = G_Task;
	MetaConfig.ImageStartTime = latestTelemetry.startTime.startTime;; 
	MetaConfig.ImagingDuration=(uint8_t)(G_Duration*10); // Imaging Duration require 4 bytes
	MetaConfig.ConfigAndTDIFile=(G_JSON);// TDIFIle require separate parameter(1 Byte)
	// MetaConfig.Latitude=1.0;
	// MetaConfig.Longitude=1.0;
}

void UI::convertUI2Arg(std::string parameter, int& argc, char**& argv) {
    // Split the input string into tokens
    std::istringstream iss(parameter);
    std::vector<std::string> tokens(std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>());

    // Set argc
    argc = static_cast<int>(tokens.size());

    // Create argv array
    argv = new char*[argc + 1]; // +1 for the nullptr at the end

	argv[0] = new char[16];
	std::strcpy(argv[0], "XDLX_KayaCam_V6");
    for (int i = 0; i < argc; i++) {
        argv[i+1] = new char[tokens[i].size() + 1];
        std::strcpy(argv[i+1], tokens[i].c_str());
    }
}



bool UI::Input_Validation_DefaultSetting()
{
	bool InArgErr= 0, InTimeErr=0, InValErr=0;
	int64_t msec=0;
	
	if(G_TotArg<7)
		InArgErr= 1;

	// msec= TimeDifferenceUpdated(&G_Time, 1);
	// if((msec<= MIN_TIME_TH_MSEC ) || (msec> MAX_TIME_TH_MSEC )) //Invalid time boundary
	// {
	// 	time_t tt; 
	// 	std::tm* ti; 
	// 	time(&tt); 
	// 	tt= tt+ADDED_DELAY;
	// 	ti = localtime(&tt);

	// 	G_Time.tm_mday= ti->tm_mday;
	// 	G_Time.tm_mon=  ti->tm_mon+1;
	// 	G_Time.tm_year= ti->tm_year+1900;
	// 	G_Time.tm_hour= ti->tm_hour;
	// 	G_Time.tm_min=  ti->tm_min;
	// 	G_Time.tm_sec=  ti->tm_sec;
	// 	auto now = system_clock::now();
	// 	auto sysMsecEpoc = duration_cast<milliseconds>(now.time_since_epoch()).count();
	// 	G_msec = sysMsecEpoc%1000;
	// 	InTimeErr= 1;
	// }

	if(G_JSON>15){
		G_JSON = 255; //255 Represents default 
		InValErr=1;
	}
	G_TDI_Stage= G_TDI>>2;
	G_TDI_Modes= G_TDI & 0x03;
	if(G_TDI_Modes==0 || G_TDI_Modes==3){
		G_TDI_Modes = 0;
		G_TDI_Stage = 1;
	}
	else{
		if(G_TDI_Stage>32){
			G_TDI_Stage = 1;
			InValErr=1;
		}	
	}
	if(G_Duration<=0 || G_Duration > MAX_DURATION){
		G_Duration = 4;
	}
	if(G_Band>127 || G_Band<=0){
		G_Band = 127;
		InValErr=1;
	}

	if(G_Gain > MAX_GAIN || G_Gain < 0.0){
		G_Gain = 1;
		InValErr=1;
	}

	if(G_BandXShift < MIN_BAND_SHIFT || G_BandXShift > MAX_BAND_SHIFT){
		G_BandXShift = 0;
		InValErr=1;
	}
	// if(G_BandYShift < MIN_BAND_SHIFT || G_BandYShift > MAX_BAND_SHIFT){
	// 	G_BandYShift = 0;
	// 	InValErr=1;
	// }


	if(G_Bin>255 || G_Bin<0){
		
		G_Bin = 247;//Except Guard Band
		InValErr=1;
	}
	return((InArgErr|| InTimeErr || InValErr)? false : true);
}


