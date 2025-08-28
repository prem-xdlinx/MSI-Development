//This code contains the combined version of packetization and frame formation
#include <iostream>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include "XDLX_Pack.h"

#define FRAME_LEN 1404
#define HDR1_LEN    6   //Primary Header Length
#define SECONDARY_FLAG
#ifdef  SECONDARY_FLAG
    #define HDR2_LEN 4      //Secondary Header Length
#else
    #define HDR2_LEN 0
#endif
#define DATA_LEN (FRAME_LEN-HDR1_LEN-HDR2_LEN)

#define IDLE_FRAMES 2
#define STREAM0 0
#define STREAM1 1

#define PRIMRY_HDR_LEN  6
#define SECOND_HDR_LEN  300
#define LINES_PER_PACKET  4
#define BINNING_FACTOR  2
using json = nlohmann::json;
namespace fs = std::filesystem;

struct commonParameters{
    int width;
    int Height;
    int totalFrames;
    int binning;
    int TDIMode;
    int TDIStages;
    int bandHeight;
    std::string mode;
    int Overlap = 0; // Added for Overlap parameter
    int packetSize = 10240;
    uint8_t MCID = 0;
};
commonParameters param;

#pragma pack(push, 1)
typedef struct _METADATA_CHUNKS {
    char XDLX[4];
    char SAT_ID[4];
    uint16_t OrbitNumber;
    uint32_t Task_ID;
    uint32_t ImageStartTime;
    uint8_t ImagingDuration;
    uint8_t ConfigAndTDIFile;
    float Latitude;
    float Longitude;
    uint32_t PPSRef;
    uint64_t TimeRef;
    uint64_t TimeCounter;
    uint8_t bandsUsed;
    uint8_t band1Active;
    uint8_t band2Active;
    uint8_t band3Active;
    uint8_t band4Active;
    uint8_t band5Active;
    uint8_t band6Active;
    uint8_t band7Active;
} METADATA_CHUNKS;
#pragma pack(pop)

uint8_t getApid(const fs::path& filename){
    // fs::path filePath(filename) //if file type is string
    std::string ext = filename.extension().string();
    if (ext == ".band00") return 0;
    if (ext == ".band01") return 1;
    if (ext == ".band02") return 2;
    if (ext == ".band10") return 10;
    if (ext == ".band11") return 11;
    if (ext == ".band12") return 12;
    if (ext == ".band20") return 20;
    if (ext == ".band21") return 21;
    if (ext == ".band22") return 22;
    if (ext == ".band30") return 30;
    if (ext == ".band31") return 31;
    if (ext == ".band32") return 32;
    if (ext == ".band40") return 40;
    if (ext == ".band41") return 41;
    if (ext == ".band42") return 42;
    if (ext == ".band50") return 50;
    if (ext == ".band51") return 51;
    if (ext == ".band52") return 52;
    if (ext == ".band60") return 60;
    if (ext == ".band61") return 61;
    if (ext == ".band62") return 62;

    if (ext == ".raw0") return 70;
    if (ext == ".raw1") return 71;

    if (ext == ".log") return 72;

    return 255;
}

void decodeHeader(std::vector<uint8_t> header){
    constexpr size_t PRIMARY_HDR_LEN = 6;
    constexpr size_t META_LEN = sizeof(METADATA_CHUNKS);
    constexpr size_t SEC_HDR_LEN_FIELD_SIZE = 2;
    constexpr size_t PARAM_COUNT_BAND = 16;
    constexpr size_t PARAM_COUNT_RAW = 16;
    size_t paramOffset_Band = PRIMARY_HDR_LEN + SEC_HDR_LEN_FIELD_SIZE + META_LEN;
    size_t paramOffset_Raw = PRIMARY_HDR_LEN + SEC_HDR_LEN_FIELD_SIZE;
    if (header.size() < PRIMARY_HDR_LEN) {
        std::cerr << "Header too small to contain metadata\n";
        return;
    }

    // --- 1) PRIMARY HEADER (bytes 0–5) ---
    uint8_t  secondaryHeader = header[0];            // == 8
    uint8_t  apID            = header[1];            // your APID byte
    uint16_t seqControl      = header[2]  | (header[3] << 8);
    uint8_t  seqFlag         = (seqControl >> 14) & 0x03;
    uint16_t seqCount        = seqControl & 0x3FFF;
    uint16_t dataLenField    = header[4] | (header[5] << 8);
    uint32_t dataLength      = dataLenField + 1;     // CCSDS: actual = field + 1

    std::cout << "=== Primary Header ===\n";
    std::cout << "Secondary Header flag: " << int(secondaryHeader) << "\n";
    std::cout << "APID:                   " << int(apID)            << "\n";
    std::cout << "Sequence Flag:         " << int(seqFlag)         << "\n";
    std::cout << "Sequence Count:        " << seqCount             << "\n";
    std::cout << "Data Length field:     " << dataLenField         << "\n";
    std::cout << "Actual Data Length:    " << dataLength           << "\n\n";

    if(secondaryHeader){
        uint16_t SecondaryLen = (header[6] & 0XFF) | (header[7] << 8);
        std::cout<<"Secondary Header Length: "<<SecondaryLen<<std::endl;
        if(apID != 70 && apID != 71){
            // --- 2) METADATA_CHUNKS (next 56 bytes) ---
            METADATA_CHUNKS metadataChunks;
            std::memcpy(&metadataChunks, &header[PRIMARY_HDR_LEN+2], META_LEN);

            printf("Raw Metadata:\n");
            printf("XDLX: %.4s\n", metadataChunks.XDLX);
            printf("SAT_ID: %.4s\n", metadataChunks.SAT_ID);
            printf("OrbitNumber: %d\n", metadataChunks.OrbitNumber);
            printf("Task_ID: %u\n", metadataChunks.Task_ID);
            printf("ImageStartTime(msec): %u\n", metadataChunks.ImageStartTime);
            printf("ImagingDuration(disec): %u\n", metadataChunks.ImagingDuration);
            printf("ConfigAndTDIFile: %u\n", metadataChunks.ConfigAndTDIFile);
            printf("Latitude: %f\n", metadataChunks.Latitude);
            printf("Longitude: %f\n", metadataChunks.Longitude);
            printf("PPSRef: %u\n", metadataChunks.PPSRef);
            printf("TimeRef: %ld\n", metadataChunks.TimeRef);
            printf("TimeCounter: %ld\n", metadataChunks.TimeCounter);
            printf("bandsUsed: %u\n", metadataChunks.bandsUsed);
            printf("band1Active: %u\n", metadataChunks.band1Active);
            printf("band2Active: %u\n", metadataChunks.band2Active);
            printf("band3Active: %u\n", metadataChunks.band3Active);
            printf("band4Active: %u\n", metadataChunks.band4Active);
            printf("band5Active: %u\n", metadataChunks.band5Active);
            printf("band6Active: %u\n", metadataChunks.band6Active);
            printf("band7Active: %u\n", metadataChunks.band7Active); 

            // ----3) Parameters bytes = "width, bandHeight,Height,TDIMode,TDIStages, binnng, LinesPerPacket, totalFrames "
            // Use little-endian decoding for all fields
            int16_t width       = header[paramOffset_Band + 0] | (header[paramOffset_Band + 1] << 8);
            int16_t bandHeight  = header[paramOffset_Band + 2] | (header[paramOffset_Band + 3] << 8);
            int16_t height      = header[paramOffset_Band + 4] | (header[paramOffset_Band + 5] << 8);
            int16_t TDIMode     = header[paramOffset_Band + 6] | (header[paramOffset_Band + 7] << 8);
            int16_t TDIStages   = header[paramOffset_Band + 8] | (header[paramOffset_Band + 9] << 8);
            int16_t binning     = header[paramOffset_Band + 10] | (header[paramOffset_Band + 11] << 8);
            int16_t linesPerPkt = header[paramOffset_Band + 12] | (header[paramOffset_Band + 13] << 8);
            int16_t totalFrames = header[paramOffset_Band + 14] | (header[paramOffset_Band + 15] << 8);


            std::cout << "Decoded Params:\n";
            std::cout << "  Width: " << (int)width << "\n";
            std::cout << "  BandHeight: " << (int)bandHeight << "\n";
            std::cout << "  Height: " << (int)height << "\n";
            std::cout << "  TDIMode: " << (int)TDIMode << "\n";
            std::cout << "  TDIStages: " << (int)TDIStages << "\n";
            std::cout << "  Binning: " << (int)binning << "\n";
            std::cout << "  LinesPerPacket: " << (int)linesPerPkt << "\n";
            std::cout << "  TotalFrames: " << (int)totalFrames << "\n";

            // --- 4) Remaining bytes = "mode" string (if any) ---
            size_t modeStart = PRIMARY_HDR_LEN + SEC_HDR_LEN_FIELD_SIZE + META_LEN + PARAM_COUNT_BAND;
            if (modeStart < header.size()) {
                std::string mode( header.begin() + modeStart, header.end() );
                std::cout << "=== Mode String ===\n";
                std::cout << mode << "\n";
            }
        }
        else if(apID == 70 || apID == 71){
            // ----3) Parameters bytes = "width,Height,TDIMode,TDIStages, binnng, LinesPerPacket, overlap, totalFrames "
            // Use little-endian decoding for all fields
            int16_t width         = header[paramOffset_Raw + 0]  | (header[paramOffset_Raw + 1] << 8);
            int16_t height        = header[paramOffset_Raw + 2]  | (header[paramOffset_Raw + 3] << 8);
            int16_t TDIMode       = header[paramOffset_Raw + 4]  | (header[paramOffset_Raw + 5] << 8);
            int16_t TDIStages     = header[paramOffset_Raw + 6]  | (header[paramOffset_Raw + 7] << 8);
            int16_t binning       = header[paramOffset_Raw + 8]  | (header[paramOffset_Raw + 9] << 8);
            int16_t LinesPerPacket= header[paramOffset_Raw + 10] | (header[paramOffset_Raw + 11] << 8);
            int16_t Overlap       = header[paramOffset_Raw + 12] | (header[paramOffset_Raw + 13] << 8);
            int16_t totalFrames   = header[paramOffset_Raw + 14] | (header[paramOffset_Raw + 15] << 8);

            std::cout << "Decoded Params:\n";
            std::cout << "  Width: " << (int)width << "\n";
            std::cout << "  Height: " << (int)height << "\n";
            std::cout << "  TDIMode: " << (int)TDIMode << "\n";
            std::cout << "  TDIStages: " << (int)TDIStages << "\n";
            std::cout << "  Binning: " << (int)binning << "\n";
            std::cout << "  LinesPerPacket: " << (int)LinesPerPacket << "\n";
            std::cout << "  Overlap: " << (int)Overlap << "\n";
            std::cout << "  TotalFrames: " << (int)totalFrames << "\n";

            // --- 4) Remaining bytes = "mode" string (if any) ---
            size_t modeStart = PRIMARY_HDR_LEN + SEC_HDR_LEN_FIELD_SIZE + PARAM_COUNT_RAW;
            if (modeStart < header.size()) {
                std::string mode( header.begin() + modeStart, header.end() );
                std::cout << "=== Mode String ===\n";
                std::cout << mode << "\n";
            }
        }else{
            size_t modeStart = PRIMARY_HDR_LEN+2 ;
            if (modeStart < header.size()) {
                std::string mode( header.begin() + modeStart, header.end() );
                std::cout << "=== Mode String ===\n";
                std::cout << mode << "\n";
            }
        }
    }
}

void decodeAndPrintHeader(const std::vector<uint8_t>& header) {
    // if (header.size() != 6) {
    //     std::cerr << "Invalid header size!" << std::endl;
    //     return;
    // }

    // uint8_t version = (header[0] >> 6) & 0x03;
    // uint16_t mcid = ((header[0] & 0x3F) << 4) | ((header[1] >> 4) & 0x0F);
    // uint8_t vcid = (header[1] >> 1) & 0x07;
    // bool ocfFlag = header[1] & 0x01;
    // uint8_t mcidCounter = header[2] & 0xFF;
    // uint8_t vcidCounter = header[3] & 0xFF;
    // bool secondaryHeaderFlag = (header[4] >> 7) & 0x01;
    // bool syncFlag = (header[4] >> 6) & 0x01;
    // bool packetOrderFlag = (header[4] >> 5) & 0x01;
    // uint8_t segmentLength = (header[4] >> 3) & 0x03;
    // uint16_t fhp = ((header[4] & 0x07) << 8) | header[5];

    uint16_t word = header[0] | (header[1] << 8);  // LE decode

    uint8_t version = (word >> 14) & 0x03;     // bits 15–14
    uint16_t mcid   = (word >> 4)  & 0x03FF;   // bits 13–4
    uint8_t vcid    = (word >> 1)  & 0x07;     // bits 3–1
    bool ocfFlag    = (word >> 0)  & 0x01;     // bit 0

    // Frame counters
    uint8_t mcidCounter = header[2];
    uint8_t vcidCounter = header[3];

    // Read in little-endian
    uint16_t word2 = header[4] | (header[5] << 8);

    // Extract using MSB-first layout
    bool secondaryHeaderFlag = (word2 >> 15) & 0x01;   // bit 15
    bool syncFlag            = (word2 >> 14) & 0x01;   // bit 14
    bool packetOrderFlag     = (word2 >> 13) & 0x01;   // bit 13
    uint8_t segmentLength    = (word2 >> 11) & 0x03;   // bits 12–11
    uint16_t fhp             = word2 & 0x07FF;         // bits 10–0

    std::cout << "Decoded Header:" << std::endl;
    std::cout << "  Version: " << (int)version << std::endl;
    std::cout << "  MCID: " << mcid << std::endl;
    std::cout << "  VCID: " << (int)vcid << std::endl;
    std::cout << "  OCF Flag: " << ocfFlag << std::endl;
    std::cout << "  MCID Counter: " << (int)mcidCounter << std::endl;
    std::cout << "  VCID Counter: " << (int)vcidCounter << std::endl;
    std::cout << "  Secondary Header Flag: " << secondaryHeaderFlag << std::endl;
    std::cout << "  Sync Flag: " << syncFlag << std::endl;
    std::cout << "  Packet Order Flag: " << packetOrderFlag << std::endl;
    std::cout << "  Segment Length: " << (int)segmentLength << std::endl;
    std::cout << "  FHP: ";
    if (fhp == 0x7FE)
        std::cout << fhp << std::endl;
    else
        std::cout << fhp << std::endl;
}

std::string getExtFile(const fs::path& folderPath, const std::string& ext){
    for(const auto& entry : fs::directory_iterator(folderPath)){
        if(entry.path().extension() == ext){
            std::cout<<"Found "<<ext<<" file: "<<entry.path()<<std::endl;
            return entry.path().string();
        }
    }
    return "";
}

json getJson(const fs::path& folderPath){
    for(const auto& entry : fs::directory_iterator(folderPath)){
        if(entry.path().extension() == ".json"){
            // std::cout<<"Found json file: "<<entry.path()<<std::endl;
            std::ifstream jsonFile(entry.path());
            if(jsonFile.is_open()){
                json j;
                jsonFile >> j;
                return j;
            }else{
                std::cerr<<"[E102] failed to open JSON file: "<<entry.path()<<std::endl;
            }
        }
    }
    return {};
}

bool fillStructureData(const json& config){

    try{
        param.width = config["Width"].get<int>() / 2;
        param.Height = config["Height"].get<int>();
        param.totalFrames = config["TotalFrames"].get<int>();
        param.binning = config["Binning"].get<int>();
        param.TDIMode = config["TDIMode"].get<int>();
        param.TDIStages = config["TDIStages"].get<int>();
        param.bandHeight = config["BandHeight"].get<int>();
        param.mode = config["ProcMode"];
        if (config.contains("Overlap")) {
            param.Overlap = config["Overlap"].get<int>();
        }
        return true;
    }
    catch(std::exception& e){
        std::cout<<"Error: Problem in parsing - " + std::string(e.what());
        return false;
    }
}

std::vector<uint8_t> readRawFile(const std::string& filePath){
    if(!fs::exists(filePath)){
        std::cout<<"[E107] Error: File not found: " + filePath<<std::endl;
        return {};
    }
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);  // Open at the end to get file size
    if (!file) {
        std::cout<<"[E108] Error: Could not open file " + filePath<<std::endl;
        return {};
    }
    size_t fileSize = file.tellg();
    file.seekg(0,std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    if(!file){
        std::cout<<"[E109] Error: Could not read entire file " + filePath<<std::endl;
        return {};
    }
    std::cout<<"File read successful: "<<filePath<<" With filesize: "<<fileSize<<std::endl;
    file.close();
    return buffer;
}

std::vector<uint8_t> packBandData(const std::vector<uint8_t>& data, const std::vector<uint8_t>& Meta, uint8_t apID) {
    int LinesPerPacket=LINES_PER_PACKET; 
    size_t metaCopyLen = 0;
    int bitsPerByte = 8, bitdepth = 10, extraParamsLen = 16, bandHeight;
    
    if(apID == 2 || apID == 12 || apID == 22 || apID == 32 || apID == 42 || apID == 52 || apID == 62){
        bandHeight = param.bandHeight / 2;
    }else{
        bandHeight = param.bandHeight;
    }

    if(bandHeight%LinesPerPacket != 0)//Hight must be integer multiple of LINES_PER_PACKET
    {
        LinesPerPacket = 1;
    }
    std::cout<<"Lines per packet is: "<<LinesPerPacket<<" for APID: "<<static_cast<int>(apID)<<std::endl;
    int16_t paramVals[8] = {
        static_cast<int16_t>(param.width),
        static_cast<int16_t>(bandHeight),
        static_cast<int16_t>(param.Height),
        static_cast<int16_t>(param.TDIMode),
        static_cast<int16_t>(param.TDIStages),
        static_cast<int16_t>(param.binning),
        static_cast<int16_t>(LinesPerPacket),
        static_cast<int16_t>(param.totalFrames)
    };
    size_t expectedSize = ((param.width * bitdepth) / bitsPerByte) * bandHeight * param.totalFrames;
    if(expectedSize != data.size()){
        std::cout<<"[E110] Insufficient Data for APID: "<<static_cast<int>(apID) <<" Data size: "<<data.size()<<" does not match expected size: "<<expectedSize<<" based on parameters."<<std::endl;
        std::cout<<"Width: "<<param.width<<", BandHeight: "<<bandHeight<<", TotalFrames: "<<param.totalFrames<<std::endl;
        return {};
    }
    size_t packetSize = ((param.width*bitdepth)/bitsPerByte)*LinesPerPacket; 
    std::cout<<"PacketSize before secondaryHeader: "<<packetSize<<" for APID: "<<static_cast<int>(apID)<<std::endl;
    size_t packetsPerFrame = bandHeight/LinesPerPacket; 
    size_t totalPackets = packetsPerFrame*param.totalFrames;
    int MetaAddr = 0, MetaLen = 56, k=0 ;
    size_t dataPos = 0, j=0;
    uint8_t seqFlag = 0b01;
    size_t extraParamsOffset = PRIMRY_HDR_LEN + 2 + MetaLen;
    int secondaryDataSize = MetaLen + param.mode.size() + extraParamsLen; // total secondary data size
    int SecondaryHeaderSize = secondaryDataSize + 2; // +2 bytes for the length field
    param.packetSize = packetSize + SecondaryHeaderSize;
    std::cout<<"PacketSize after secondaryHeader: "<<param.packetSize<<" for APID: "<<static_cast<int>(apID)<<std::endl;
    // std::vector<uint8_t> flatBuffer;
    // flatBuffer.reserve(totalPackets * (param.packetSize+6));
    size_t fullPacketSize = param.packetSize + PRIMRY_HDR_LEN;
    std::vector<uint8_t> flatBuffer(totalPackets *fullPacketSize, 0);
    std::cout<<"Buffer size allocated: "<<flatBuffer.size()<<" for APID: "<<static_cast<int>(apID)<<std::endl;

    for (int i = 0, k = 0; i < totalPackets; i++) {
        std::vector<uint8_t> header(PRIMRY_HDR_LEN + SecondaryHeaderSize, 0);
        header[0] = 8;            // Secondary header flag
        header[1] = apID;         // APID

        uint16_t sequenceCount = (seqFlag << 14) | (i & 0x3FFF);
        header[2] = sequenceCount & 0xFF;
        header[3] = (sequenceCount >> 8) & 0xFF;

        uint16_t dataLength = param.packetSize - 1;
        header[4] = dataLength & 0xFF;
        header[5] = (dataLength >> 8) & 0xFF;

        // Secondary header length
        header[PRIMRY_HDR_LEN]     = SecondaryHeaderSize & 0xFF;
        header[PRIMRY_HDR_LEN + 1] = (SecondaryHeaderSize >> 8) & 0xFF;

        // Metadata
        size_t metaCopyLen = (MetaAddr < Meta.size()) ? std::min(static_cast<size_t>(MetaLen), Meta.size() - MetaAddr) : 0;
        std::copy(Meta.begin() + MetaAddr, Meta.begin() + MetaAddr + metaCopyLen, header.begin() + PRIMRY_HDR_LEN + 2);
        if (metaCopyLen < MetaLen) {
            std::fill(header.begin() + PRIMRY_HDR_LEN + 2 + metaCopyLen,
                      header.begin() + PRIMRY_HDR_LEN + 2 + MetaLen,
                      0x00);  // fill with 0x00
        }
        // Extra params
        size_t paramWritePos = PRIMRY_HDR_LEN + 2 + MetaLen;

        for (int p = 0; p < 8; ++p) {
            header[paramWritePos++] = paramVals[p] & 0xFF;
            header[paramWritePos++] = (paramVals[p] >> 8) & 0xFF;
        }

        // Mode string
        std::copy(param.mode.begin(), param.mode.end(), header.begin() + paramWritePos);

        // Data payload
        size_t offset = i * fullPacketSize; 
        std::copy(header.begin(), header.end(), flatBuffer.begin() + offset);

        // Data payload
        size_t copySize = std::min(packetSize, data.size() - dataPos);
        std::copy(data.begin() + dataPos, data.begin() + dataPos + copySize, flatBuffer.begin() + offset + header.size());

        // Pad if needed
        if (copySize < packetSize) {
            std::fill(flatBuffer.begin() + offset + header.size() + copySize,
                    flatBuffer.begin() + offset + header.size() + packetSize, 0x00);
        }
        dataPos += copySize;
        // dataPos += packetSize;

        if (++k == packetsPerFrame) {
            if (MetaAddr + MetaLen < Meta.size()) MetaAddr += MetaLen;
            k = 0;
        }
        // decodeHeader(header);
        j++;
    }

    std::cout << "Total Packets: " << j << " | Buffer Size: " << flatBuffer.size()<<" for APID: "<<static_cast<int>(apID) << std::endl;
    return flatBuffer;
}

std::vector<uint8_t> packRawData(const std::vector<uint8_t>& data, uint8_t apID) {
    int LinesPerPacket=LINES_PER_PACKET; 
    int bitsPerByte = 8, bitdepth = 10, extraParamsLen = 16;
    if(param.Height%LinesPerPacket != 0)//Hight must be integer multiple of LINES_PER_PACKET
    {
        LinesPerPacket = 1;
    }
    size_t packetPayloadSize = (((param.width*2)*bitdepth)/bitsPerByte)*LinesPerPacket; 
    std::cout<<"PacketSize before secondaryHeader: "<<packetPayloadSize<<std::endl;
    size_t totalSize = data.size();
    int16_t paramVals[8] = {
        static_cast<int16_t>(param.width*2), // width is doubled for raw data (Initially made half for band data)
        static_cast<int16_t>(param.Height),
        static_cast<int16_t>(param.TDIMode),
        static_cast<int16_t>(param.TDIStages),
        static_cast<int16_t>(param.binning),
        static_cast<int16_t>(LinesPerPacket),
        static_cast<int16_t>(param.Overlap), 
        static_cast<int16_t>(param.totalFrames)
    };
    size_t expectedSize = ((param.width * bitdepth) / bitsPerByte) * param.Height * param.totalFrames;
    if(expectedSize != data.size()){
        std::cout<<"[E110] Insufficient Data: Data size: "<<data.size()<<" does not match expected size: "<<expectedSize<<" based on parameters."<<std::endl;
        std::cout<<"Width: "<<param.width<<", BandHeight: "<<param.Height<<", TotalFrames: "<<param.totalFrames<<std::endl;
        return {};
    }
    int secondaryDataSize = param.mode.size() + extraParamsLen;
    int secondaryHeaderSize = secondaryDataSize + 2;
    param.packetSize = packetPayloadSize + secondaryHeaderSize;
    std::cout<<"PacketSize after secondaryHeader: "<<param.packetSize<<std::endl;
    size_t extraParamsOffset = PRIMRY_HDR_LEN + 2;
    size_t headerSize = PRIMRY_HDR_LEN + secondaryHeaderSize;
    size_t totalPackets = (totalSize + packetPayloadSize - 1) / packetPayloadSize;
    size_t fullPacketSize = headerSize + packetPayloadSize;

    // Pre-allocate the output buffer for speed
    std::vector<uint8_t> flatPackets(totalPackets * fullPacketSize, 0);

    size_t dataPos = 0;
    for (size_t j = 0; j < totalPackets; ++j) {
        size_t chunkSize = std::min(packetPayloadSize, totalSize - dataPos);
        size_t offset = j * fullPacketSize;

        // Build header
        std::vector<uint8_t> header(headerSize, 0);
        uint8_t seqFlag = (j == 0) ? 0b01 : ((dataPos + chunkSize >= totalSize) ? 0b10 : 0b00);
        header[0] = 0x08;
        header[1] = apID & 0xFF;

        uint16_t sequenceCount = (seqFlag << 14) | (j & 0x3FFF);
        header[2] = sequenceCount & 0xFF;
        header[3] = (sequenceCount >> 8) & 0xFF;
        
        uint16_t dataLength = chunkSize + secondaryHeaderSize - 1;
        header[4] = dataLength & 0xFF;
        header[5] = (dataLength >> 8) & 0xFF;
        
        header[PRIMRY_HDR_LEN]     = secondaryHeaderSize & 0xFF;
        header[PRIMRY_HDR_LEN + 1] = (secondaryHeaderSize >> 8) & 0xFF; 

        // Insert extra params
        size_t paramWritePos = extraParamsOffset;
        for (int p = 0; p < 8; ++p) {
            header[paramWritePos++] = paramVals[p] & 0xFF;
            header[paramWritePos++] = (paramVals[p] >> 8) & 0xFF; 
        }

        // Insert mode string
        std::copy(param.mode.begin(), param.mode.end(), header.begin() + extraParamsOffset + extraParamsLen);

        // Copy header to output buffer
        std::copy(header.begin(), header.end(), flatPackets.begin() + offset);

        // Copy data to output buffer
        std::copy(data.begin() + dataPos, data.begin() + dataPos + chunkSize, flatPackets.begin() + offset + header.size());

        // Pad with zeros if needed
        if (chunkSize < packetPayloadSize) {
            std::fill(flatPackets.begin() + offset + header.size() + chunkSize,
                      flatPackets.begin() + offset + header.size() + packetPayloadSize, 0x00);
        }
        // decodeHeader(header);
        dataPos += chunkSize;
    }

    std::cout << "Total Packets: " << totalPackets << std::endl;

    return flatPackets;
}

std::vector<uint8_t> packLogData(const std::vector<uint8_t>& data, uint8_t apID) {
    size_t totalSize = data.size();
    size_t packetPayloadSize = param.packetSize;
    size_t totalPackets = (totalSize + packetPayloadSize - 1) / packetPayloadSize;
    size_t fullPacketSize = 6 + packetPayloadSize; // 6 = header size
    std::cout<<"Packet Payload Size for logFile: "<<packetPayloadSize<<std::endl;
    std::vector<uint8_t> flatBuffer(totalPackets * fullPacketSize, 0);

    uint16_t seqCount = 0;
    for (size_t i = 0; i < totalSize; i += packetPayloadSize) {
        size_t chunkSize = std::min(packetPayloadSize, totalSize - i);
        size_t offset = (seqCount) * fullPacketSize;

        // Build header
        std::vector<uint8_t> header(6, 0);
        uint8_t seqFlag = (i == 0) ? 0b01 : ((i + chunkSize >= totalSize) ? 0b10 : 0b00);
        header[0] = 0x00;                  // No secondary header
        header[1] = apID & 0xFF;
        uint16_t sequence = (seqFlag << 14) | (seqCount & 0x3FFF);
        header[2] = sequence & 0xFF;
        header[3] = (sequence >> 8) & 0xFF;
        uint16_t dataLength = chunkSize - 1;  // CCSDS = total data length - 1
        header[4] = dataLength & 0xFF;
        header[5] = (dataLength >> 8) & 0xFF;

        // Copy header
        std::copy(header.begin(), header.end(), flatBuffer.begin() + offset);

        // Copy data chunk
        std::copy(data.begin() + i, data.begin() + i + chunkSize, flatBuffer.begin() + offset + header.size());

        // Pad with 0 if needed
        if (chunkSize < packetPayloadSize) {
            std::fill(flatBuffer.begin() + offset + header.size() + chunkSize,
                      flatBuffer.begin() + offset + header.size() + packetPayloadSize, 0x00);
        }
        // decodeHeader(header);
        seqCount++;
    }

    std::cout << "Total Packets: " << seqCount << std::endl;
    return flatBuffer;
}

void writeLE16(uint8_t* dest, uint16_t value) {
    dest[0] = value & 0xFF;
    dest[1] = (value >> 8) & 0xFF;
}

void writeIdleFrames(std::ofstream& out, int streamId) {
    static uint8_t VCIDIdleS1Cnt = 0, VCIDIdleS2Cnt = 0;
    std::vector<uint8_t> Idle(FRAME_LEN, 0xAA); //Pre-Allocate buffer
    if(streamId == STREAM0){
        writeLE16(&Idle[0], 0x2AA2);
        writeLE16(&Idle[4], 0x1FFE);

        for (int i = 0; i < IDLE_FRAMES; ++i) {
            Idle[2] = param.MCID++;
            Idle[3] = VCIDIdleS1Cnt++;
            // decodeAndPrintHeader(Idle) ;
            out.write(reinterpret_cast<const char*>(Idle.data()), FRAME_LEN);
        }
    }
    if(streamId == STREAM1){
        writeLE16(&Idle[0], 0x2AAA);
        writeLE16(&Idle[4], 0x1FFE);

        for (int i = 0; i < IDLE_FRAMES; ++i) {
            Idle[2] = param.MCID++;
            Idle[3] = VCIDIdleS2Cnt++;
            // decodeAndPrintHeader(Idle) ;
            out.write(reinterpret_cast<const char*>(Idle.data()), FRAME_LEN);
        }
    }

}
void packFHP(std::vector<uint8_t>& frame, uint16_t FHP, uint8_t segmentLen){
    uint16_t temp;
    segmentLen = segmentLen & 0x3;
    FHP = FHP & 0x07FF;  // Mask to 11 bits

    #ifdef SECONDARY_FLAG
        temp = 0x8000;  // e.g., Secondary header flag + other bits
    #else
        temp = 0x0;  // Primary header only
    #endif

    // temp = temp | segmentLen | FHP;  // <-- Use OR to embed FHP bits
    temp |= (segmentLen << 11);  // Place segmentLen at bits 12–11
    temp |= FHP;                 // Place FHP at bits 10–0

    frame[4] = (uint8_t)(temp & 0x00FF);         // Low byte
    frame[5] = (uint8_t)((temp >> 8) & 0x00FF);  // High byte
}


void writeDataFramesFromBuffer(std::ofstream& outFile, const uint8_t* pktBuff, size_t fileSize, int pktLen, int streamId) {
    // --- VCID handling ---
    static int lastStreamId = -1;
    static uint8_t lastVCID = 0;
    uint8_t VCIDCnt = (streamId == lastStreamId) ? lastVCID : 0;
    lastStreamId = streamId;

    const int HDR_LEN = HDR1_LEN + HDR2_LEN;
    const size_t MAX_BUFFERED_FRAMES = 64;
    // --- FHP and segmentLen ---
    uint8_t segmentLen = 0;
    #ifdef SECONDARY_FLAG
        uint16_t FHP = HDR2_LEN;
    #else
        uint16_t FHP = 0;
    #endif

    // Pre-initialize frame with 0x55
    std::vector<uint8_t> frame(FRAME_LEN, 0x55);
    uint16_t headerWord = (streamId == STREAM0) ? 0x2AA0 : 0x2AA8;

    // Write to buffer in little endian
    frame[0] = headerWord & 0xFF;
    frame[1] = (headerWord >> 8) & 0xFF;

    // Buffer to hold multiple frames
    std::vector<uint8_t> writeBuffer;
    writeBuffer.reserve(MAX_BUFFERED_FRAMES * FRAME_LEN);

    size_t i = 0, pktBytesWritten = 0, totalSize = 0, j=0;

    while (i < fileSize) {
        size_t nFrameBytes = std::min((size_t)DATA_LEN, fileSize - i);

        if (pktBytesWritten == 0) {
            FHP = HDR2_LEN;
            segmentLen = 1;
        } else if (pktBytesWritten + nFrameBytes <= pktLen) {
            FHP = 0x7FF;
            segmentLen = 0;
        } else {
            FHP = HDR2_LEN + (pktLen - pktBytesWritten); //offsetInFrame = pktLen - pktBytesWritten;
            segmentLen = 0;
            // std::cout<<"FHP of new packet: "<<FHP<<" Packet len: "<<pktLen<<std::endl;
        }
        if ((i + nFrameBytes) >= fileSize) {
            FHP = HDR2_LEN + (pktLen - pktBytesWritten); //offsetInFrame = pktLen - pktBytesWritten;
            segmentLen = 2;
        }

        // Fill headers
        frame[2] = param.MCID++;
        frame[3] = VCIDCnt++;
        packFHP(frame, FHP, segmentLen);
        // decodeAndPrintHeader(frame);
        // if(param.MCID > 5){
        //     break;
        // }
        // Fill data region
        std::memcpy(&frame[HDR_LEN], pktBuff + i, nFrameBytes);
        if (nFrameBytes < DATA_LEN) {
            std::fill(frame.begin() + HDR_LEN + nFrameBytes, frame.end(), 0x55);
            // std::cout<<"[W301] Warning: Frame data is less than expected. Padding with 0x55."<<std::endl;
        }

        // Append to buffer
        writeBuffer.insert(writeBuffer.end(), frame.begin(), frame.end());
        totalSize += FRAME_LEN;
        // std::cout<<"FrameSize: "<<frame.size()<<" Frame Len: "<<FRAME_LEN<<" NframeBytes: "<<nFrameBytes<<std::endl;
        pktBytesWritten += nFrameBytes;
        i += nFrameBytes;

        // Packet boundary
        while (pktBytesWritten >= pktLen) pktBytesWritten -= pktLen;

        // Flush buffer if full
        if (writeBuffer.size() >= MAX_BUFFERED_FRAMES * FRAME_LEN) {
            outFile.write(reinterpret_cast<const char*>(writeBuffer.data()), writeBuffer.size());
            writeBuffer.clear();
        }
        j++;
    }

    // Final flush
    if (!writeBuffer.empty()) {
        outFile.write(reinterpret_cast<const char*>(writeBuffer.data()), writeBuffer.size());
    }

    lastVCID = VCIDCnt;

    // Logging
    // std::cout << "Data Written: " << totalSize << " bytes" <<j<<" Total Frames Written"<< std::endl;
    if (i == fileSize) {
        std::cout << "All data processed successfully." << std::endl;
    } else {
        std::cout << "[E112] Incomplete write! Remaining: " << fileSize - i << " bytes\n";
    }
}

void processPacketBuffers(const std::map<std::string, std::vector<uint8_t>>& packetBuffers, std::ofstream& outFile, int streamId, const std::vector<std::string>& suffixes) {
    for (const auto& suffix : suffixes) {
        auto it = packetBuffers.find(suffix);
        if (it != packetBuffers.end() && !it->second.empty()) {
            std::cout << "Found packet buffer for: " << suffix << std::endl;
            std::cout << "Writing initial Idle Frames: " << IDLE_FRAMES << " for packet buffer: " << suffix << std::endl;
            writeIdleFrames(outFile, streamId);
            std::cout << "Writing data frames from packet buffer: " << suffix << std::endl;
            writeDataFramesFromBuffer(outFile, it->second.data(), it->second.size(), param.packetSize+6/*Including Header*/, streamId);
        }
    }
    std::cout << "Writing End Idle Frames:" << std::endl;
    writeIdleFrames(outFile, streamId);
}

void processPacketFiles(const fs::path& baseDir, const std::string& baseName,
                  const std::vector<std::string>& suffixes, std::string& Metafile) {
    std::mutex bufferMutex;
    std::map<std::string, std::vector<uint8_t>> packetBuffers;
    std::vector<std::thread> threads;

    // 1. Launch threads for packetization
    for (const auto& suffix : suffixes) {
        fs::path filePath = baseDir / (baseName + suffix);
        if (!fs::exists(filePath)) continue;

        threads.emplace_back([&, filePath, suffix]() {
            std::cout << "Performing packetization for file: " << filePath << std::endl;
            uint8_t apid = getApid(filePath);
            std::vector<uint8_t> data = readRawFile(filePath);
            std::vector<uint8_t> flatPackets;
            if (data.empty()) {
                std::cout << "[E106] No data in the file: " << filePath << " Skipping packetization and frame formation." << std::endl;
            }

            if (apid != 0xFF && !data.empty()) {
                if (apid != 70 && apid != 71 && apid != 72) {
                    std::vector<uint8_t> Meta = readRawFile(Metafile);
                    flatPackets = packBandData(data, Meta, apid);
                } else if (apid == 70 || apid == 71) {
                    flatPackets = packRawData(data, apid);
                } else {
                    std::cout << "[E110] Unsupported APID: " << (int)apid << " for file: " << filePath << std::endl;
                }
            }

            // Store result
            std::lock_guard<std::mutex> lock(bufferMutex);
            packetBuffers[suffix] = std::move(flatPackets);
        });
    }

    // 2. Wait for all threads to finish
    for (auto& t : threads) t.join();

    fs::path filePath = baseDir / (baseName + ".log");
    if (fs::exists(filePath)) {
        std::vector<uint8_t> flatPackets;
        std::cout << "Performing packetization for file: " << filePath << std::endl;
        uint8_t apid = getApid(filePath);
        std::vector<uint8_t> data = readRawFile(filePath);
        if(!data.empty() && apid ==72) {
            std::cout << "APID for log file: " << (int)apid << std::endl;
            flatPackets = packLogData(data, apid);
            packetBuffers[".log"] = std::move(flatPackets);
        } else {
            std::cout << "[E111] No data in the log file or unsupported APID: " << (int)apid << std::endl;
        }
    }

    std::cout << "Packetization completed." << std::endl;  
    fs::path outStreamFile1 = StageFolder / (baseName + ".stream1");
    fs::path outStreamFile2 = StageFolder / (baseName + ".stream2");

    std::ofstream outFileStream1(outStreamFile1, std::ios::binary);
    std::ofstream outFileStream2(outStreamFile2, std::ios::binary);                       
    std::vector<std::string> stream1Suffixes = {
        ".log", ".band00", ".band02", ".band10", ".band12",
        ".band20", ".band22", ".band30", ".band32", ".band40",
        ".band50", ".band60", ".raw0"
    };

    std::vector<std::string> stream2Suffixes = {
        ".log", ".band01", ".band11", ".band21", ".band31",
        ".band41", ".band42", ".band51", ".band52", ".band61",
        ".band62", ".raw2"
    };

        // Pass the map to the function
    std::cout<<"Frames are being written to file: "<<outStreamFile1<<std::endl;
    processPacketBuffers(packetBuffers, outFileStream1, STREAM0, stream1Suffixes);
    std::cout<<"Frames are being written to file: "<<outStreamFile2<<std::endl;
    processPacketBuffers(packetBuffers, outFileStream2, STREAM1, stream2Suffixes);

    outFileStream1.close();
    outFileStream2.close();
}



void processFolder(const fs::path& folderPath, std::string& jsonFilePath, std::string& logFilePath) {
    int filecount = 0, bandStatus = false;

    fs::path logPath(logFilePath);  

    std::string baseName = logPath.stem().string();
    fs::path baseDir = logPath.parent_path();

    json jsonData;
    if(fs::exists(jsonFilePath)){
        jsonData = getJson(folderPath);
        if(!jsonData.empty()){
            bool jsonStatus = fillStructureData(jsonData);
            if(!jsonStatus){
                std::cout<<"[E103] Failed to parse the data from json file."<<std::endl;
                return;
            }

        }else{
            std::cout<<"[E104] Empty JSON file(No Captures done)."<<std::endl;
        }
    }else{
        std::cout<<"[E105] JSON file not found. No Captures in current session."<<std::endl;
    }
    std::string Metafile = getExtFile(folderPath, ".meta");

    // Define suffixes for each stream                               
    std::vector<std::string> suffixes = {
        ".band00", ".band02", ".band10", ".band12",
        ".band20", ".band22", ".band30", ".band32", ".band40",
        ".band50", ".band60", ".raw0",".band01", ".band11", ".band21", ".band31",
        ".band41", ".band42", ".band51", ".band52", ".band61",
        ".band62", ".raw1"
    };

    processPacketFiles(baseDir, baseName, suffixes, Metafile);

}

void scanDirectories(const std::string& baseDir){
    if(!fs::exists(baseDir)){
        std::cout<<"[E100] Root folder not found: "<<baseDir<<std::endl;
        return;
    }
    for(const auto& entry : fs::directory_iterator(baseDir)){
        if(fs::is_directory(entry.path())){
            std::string jsonFilePath = getExtFile(entry.path(), ".json");
            std::string logFilePath = getExtFile(entry.path(), ".log");
            if (fs::exists(logFilePath) || fs::exists(jsonFilePath)) {
                std::cout<<"Processing folder: "+entry.path().string()<<std::endl;
                processFolder(entry.path(), jsonFilePath, logFilePath);
                try {
                    if (fs::remove_all(entry.path())) {
                        std::cout << "Folder deleted successfully.: "<<entry.path()<<std::endl;;
                    } else {
                        std::cout << "Folder not found or could not be deleted: "<<entry.path()<<std::endl;
                    }
                } catch (const fs::filesystem_error& e) {
                    std::cerr << "Filesystem error: " << e.what() << '\n';
                }
            }else{
                std::cout<<"[E101] LogFile or Jsonfile not found in directory: "+entry.path().string()+" Skipping Directory."<<std::endl;
            }
        }
    }
} 

int startFormation(){
    auto start = std::chrono::high_resolution_clock::now();
    std::cout<<"Packet Frame formation started."<<std::endl;
    // std::string baseDirectory = "Capture";
    scanDirectories(capturesDirectory);
    auto end = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double>(end-start).count();
    std::cout<<"Total Time took for Packetization and Frame Formation: "<<duration<<std::endl;
    return 0;
}



