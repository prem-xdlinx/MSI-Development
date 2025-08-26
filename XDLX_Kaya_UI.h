#ifndef XDLX_KAYA_UI_H
#define XDLX_KAYA_UI_H

std::vector<std::string> split(const std::string& s, char delimiter);
int64_t TimeDifferenceUpdated(std::tm *Time, bool PrintFlag);

class UI{
	private:
		std::string ParamFilePath;//[Max_File_Path_Len]= "ConfigData/param.txt";

	public:
		UI(){};
		UI(std::string FilePath);
		void WelcomeMessage();
		void Display( std::string  name);
		void PrintArgument(int& argc, char**& argv);
		void ReadUIFromFile(std::string  *parameter);
		void convertUI2Arg(std::string parameter, int& argc, char**& argv);
		bool Input_Validation_DefaultSetting();
		void ProcessInput( int& argc, char**& argv, MetaSet &MetaConfig);
		bool readInput(int& argc, char**& argv);
};

#endif
