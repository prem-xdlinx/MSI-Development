#include <iostream>
#include <thread>
#include "XDLX_CamApp_V6.h"
#include "antaris_api.h"
#include <vector>
#include <cmath>
#include <chrono>
#include <atomic>

////////////  Capture Application Dependencies ////////////
#include "sharedParams.h"
std::string userDirectory = "/home/xdlinx/Pictures/XDLX_KYFG_V6_1_Dev/";
/////////////////////////////////////////////////

//////////// Formation Directory ////////////
#include "XDLX_Pack.h"
std::filesystem::path StageFolder = "/home/xdlinx/Pictures/XDLX_KYFG_V6_1_Dev/StageFolder/";
std::string capturesDirectory = userDirectory + "Capture/";
///////////////////////////////////////////


std::vector<AdcsEphemerisData> gnss_eph_data_history;
GnssEphData latest_gnss_eph_data; // If not using vector, define a single instance
bool storeData = true;
double epsVoltage = 0.0;


std::atomic<bool> stopFillThread(false);
// ----------------------------------------------------------------
// Helper function to update all fields
// ----------------------------------------------------------------
void UpdateGnssEphData(GnssEphData &gnss_eph_data, int counter) {
    // GnssEphData main fields
    gnss_eph_data.correlation_id = 1000 + counter;
    gnss_eph_data.adcs_timeout_flag = counter % 2;
    gnss_eph_data.gps_timeout_flag  = (counter + 1) % 2;

    // ADCS Ephemeris Data
    AdcsEphemerisData &adcs = gnss_eph_data.adcs_eph_data;
    adcs.orbit_time       = counter * 10.0;
    adcs.eci_position_x   = 7000.0 + std::sin(counter) * 50;
    adcs.eci_position_y   = -1200.0 + std::cos(counter) * 50;
    adcs.eci_position_z   = 500.0 + std::sin(counter / 2.0) * 20;
    adcs.eci_velocity_x   = 1.1 + 0.01 * counter;
    adcs.eci_velocity_y   = -0.5 + 0.01 * counter;
    adcs.eci_velocity_z   = 7.3 + 0.01 * counter;
    adcs.ecef_position_x  = 6378.1 + std::sin(counter / 3.0) * 10;
    adcs.ecef_position_y  = 100.2 + std::cos(counter / 3.0) * 10;
    adcs.ecef_position_z  = 200.4 + std::sin(counter / 4.0) * 5;
    adcs.ecef_velocity_x  = 0.01 + 0.001 * counter;
    adcs.ecef_velocity_y  = 0.02 + 0.001 * counter;
    adcs.ecef_velocity_z  = 0.03 + 0.001 * counter;
    adcs.ang_rate_x       = 0.1 + 0.01 * counter;
    adcs.ang_rate_y       = 0.2 + 0.01 * counter;
    adcs.ang_rate_z       = 0.3 + 0.01 * counter;
    adcs.att_quat_1       = 1.0;
    adcs.att_quat_2       = 0.0 + 0.001 * counter;
    adcs.att_quat_3       = 0.0 + 0.001 * counter;
    adcs.att_quat_4       = 0.0 + 0.001 * counter;
    adcs.latitude         = 10.0f + std::sin(counter * 0.1f) * 5;
    adcs.longitude        = 60.0f + std::cos(counter * 0.1f) * 5;
    adcs.altitude         = 500.0f + std::sin(counter * 0.05f) * 10;
    adcs.nadir_vector_x   = std::sin(counter * 0.1f);
    adcs.nadir_vector_y   = std::cos(counter * 0.1f);
    adcs.nadir_vector_z   = -0.5f;
    adcs.gd_nadir_vector_x = std::sin(counter * 0.2f);
    adcs.gd_nadir_vector_y = std::cos(counter * 0.2f);
    adcs.gd_nadir_vector_z = -0.3f;
    adcs.beta_angle       = 45.0f + std::sin(counter * 0.1f) * 5;
    adcs.validity_flags   = 0xFFFF;

    // GPS Ephemeris Data
    GpsEphemerisData &gps = gnss_eph_data.gps_eph_data;
    gps.gps_fix_time = 123456 + counter;
    gps.gps_sys_time = 9876543210ULL + counter * 1000;

    gps.obc_time.hour = (12 + counter) % 24;
    gps.obc_time.minute = (34 + counter) % 60;
    gps.obc_time.millisecond = (567 + counter) % 1000;
    gps.obc_time.date = 15;
    gps.obc_time.month = 8;
    gps.obc_time.year = 2025;

    gps.gps_position_ecef[0] = 1000000 + counter * 10;
    gps.gps_position_ecef[1] = 2000000 + counter * 20;
    gps.gps_position_ecef[2] = 3000000 + counter * 30;

    gps.gps_velocity_ecef[0] = 100 + counter;
    gps.gps_velocity_ecef[1] = 200 + counter;
    gps.gps_velocity_ecef[2] = 300 + counter;

    gps.gps_validity_flag_pos_vel = 0x3;
}


void FillMetadata() {
    std::cout << "Filling Metadata..." << std::endl;
     GnssEphData gnss_eph_data;
    int counter = 0;

    while (!stopFillThread) {
        counter++;
        UpdateGnssEphData(gnss_eph_data, counter);
        latest_gnss_eph_data = gnss_eph_data; // Update the latest data
        if (storeData) {
            gnss_eph_data_history.push_back(gnss_eph_data.adcs_eph_data); // Store a copy in the vector
            std::cout << "Stored ADCS EPH data in history." << std::endl;
        }
        epsVoltage = 3.3 + std::sin(counter * 0.1) * 0.1; // Simulate EPS voltage
        std::cout << "[Update " << counter << "] "
                  << "Lat: " << gnss_eph_data.adcs_eph_data.latitude
                  << ", Lon: " << gnss_eph_data.adcs_eph_data.longitude
                  << ", GPS Time: " << gnss_eph_data.gps_eph_data.gps_fix_time
                  << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void ExecuteCapture() {
    // Simulate the Capture sequence execution
    std::cout << "Executing Capture Sequence..." << std::endl;
    stopFillThread = false; // Stop the metadata filling thread if it was running
    storeData = true; // Enable data storage
    std::thread MetadataFillingThread(FillMetadata);
    int argcs = 1;
    char *argvs[] = {(char*)"XDLX_CamApp_V5"};
    int status = startMSIApp(argcs, argvs);
    stopFillThread = true; // Stop the metadata filling thread after execution
    MetadataFillingThread.join(); // Wait for the metadata filling thread to finish
}
void ExecuteFormation() {
    // Simulate the Formation sequence execution
    std::cout << "Executing Formation Sequence..." << std::endl;
    int status = startFormation();
    
}
int main(int argc, char *argv[]){
    int n;
    std::cout<< "Enter Command to execute Sequence( 100 - Capture, 102 - Formation): "<<std::endl;
    do{
        std::cin >> n;
        if(n == 100){
            std::cout << "Executing Capture Sequence..." << std::endl;
            std::thread Capture(ExecuteCapture);
            Capture.detach(); // Let it run in the background
        } else if(n == 102){
            std::cout << "Executing Formation Sequence..." << std::endl;
            std::thread Formation(ExecuteFormation);
            Formation.detach(); // Let it run in the background
        } else {
            std::cerr << "Invalid command. Please enter 100 or 102." << std::endl;
            break;
        }
    }while(n == 100 || n == 102);
    
}