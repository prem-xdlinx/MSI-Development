#include "antaris_api.h"
// #define EPHIMIRUS_ARRAY

extern std::vector<AdcsEphemerisData> gnss_eph_data_history;
extern GnssEphData latest_gnss_eph_data; // If not using vector, define a single instance

extern PayloadMetricsResponse TelemetryData;


////////// Variables ////////////////////
extern std::string userDirectory;
extern bool storeData ; // Flag to indicate if data should be stored
extern double epsVoltage; // Variable to store EPS voltage