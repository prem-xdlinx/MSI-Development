import struct

# First 56 bytes: Little-endian format
METADATA_56_FORMAT = "<4s4sHII2BffIQQB7B"

eph_format = "<" + "d"*20 + "f"*10 + "H" + "x"*6


# Last 16 bytes: Big-endian (8 int16_t = 16 bytes)
PARAMS_16_FORMAT_BAND = "<10h"
PARAMS_16_FORMAT_RAW = "<8h"

def decode_ephemeris_struct(data):
    unpacked = struct.unpack(eph_format, data)
    eph_data = {
        "orbit_time": unpacked[0],
        "eci_position_x": unpacked[1],
        "eci_position_y": unpacked[2],
        "eci_position_z": unpacked[3],
        "eci_velocity_x": unpacked[4],
        "eci_velocity_y": unpacked[5],
        "eci_velocity_z": unpacked[6],
        "ecef_position_x": unpacked[7],
        "ecef_position_y": unpacked[8],
        "ecef_position_z": unpacked[9],
        "ecef_velocity_x": unpacked[10],
        "ecef_velocity_y": unpacked[11],
        "ecef_velocity_z": unpacked[12],
        "ang_rate_x": unpacked[13],
        "ang_rate_y": unpacked[14],
        "ang_rate_z": unpacked[15],
        "att_quat_1": unpacked[16],
        "att_quat_2": unpacked[17],
        "att_quat_3": unpacked[18],
        "att_quat_4": unpacked[19],
        "latitude": unpacked[20],
        "longitude": unpacked[21],
        "altitude": unpacked[22],
        "nadir_vector_x": unpacked[23],
        "nadir_vector_y": unpacked[24],
        "nadir_vector_z": unpacked[25],
        "gd_nadir_vector_x": unpacked[26],
        "gd_nadir_vector_y": unpacked[27],
        "gd_nadir_vector_z": unpacked[28],
        "beta_angle": unpacked[29],
        "validity_flags": unpacked[30],
    }
    return eph_data

def decode_Params_Band(data):
    if len(data) != 20:
        raise ValueError(f"Expected 16 bytes for Params_Band, got {len(data)}")
    unpacked_params = struct.unpack(PARAMS_16_FORMAT_BAND, data)
    params = {
        "Width": unpacked_params[0],
        "BandHeight": unpacked_params[1],
        "Height": unpacked_params[2],
        "TDIMode": unpacked_params[3],
        "TDIStages": unpacked_params[4],
        "Binning": unpacked_params[5],
        "LinesPerPacket": unpacked_params[6],
        "TotalFrames": unpacked_params[7],
        "CoreTemperature": unpacked_params[8],
        "SensorTemperature": unpacked_params[9],
    }
    return params

def decode_metadata_chunks(data, apid):
    if len(data) != 56:
        raise ValueError(f"Expected 56 bytes for metadata, got {len(data)}")
    if apid!=70 and apid!=71:
        # Step 1: Decode the first 56 bytes (little-endian)
        unpacked_meta = struct.unpack(METADATA_56_FORMAT, data)

        # Step 2: Decode the remaining 16 bytes (big-endian)
        # param_part = data[56:]
        # unpacked_params = struct.unpack(PARAMS_16_FORMAT_BAND, param_part)

        metadata = {
            "XDLX": unpacked_meta[0].decode('ascii', errors='ignore'),
            "SAT_ID": unpacked_meta[1].decode('ascii', errors='ignore'),
            "OrbitNumber": unpacked_meta[2],
            "Task_ID": unpacked_meta[3],
            "ImageStartTime": unpacked_meta[4],
            "ImagingDuration": unpacked_meta[5],
            "ConfigAndTDIFile": unpacked_meta[6],
            "Latitude": unpacked_meta[7],
            "Longitude": unpacked_meta[8],
            "PPSRef": unpacked_meta[9],
            "TimeRef": unpacked_meta[10],
            "TimeCounter": unpacked_meta[11],
            "bandsUsed": unpacked_meta[12],
            "band1Active": unpacked_meta[13],
            "band2Active": unpacked_meta[14],
            "band3Active": unpacked_meta[15],
            "band4Active": unpacked_meta[16],
            "band5Active": unpacked_meta[17],
            "band6Active": unpacked_meta[18],
            "band7Active": unpacked_meta[19],
            # "Params": {
            #     "Width": unpacked_params[0],
            #     "BandHeight": unpacked_params[1],
            #     "Height": unpacked_params[2],
            #     "TDIMode": unpacked_params[3],
            #     "TDIStages": unpacked_params[4],
            #     "Binning": unpacked_params[5],
            #     "LinesPerPacket": unpacked_params[6],
            #     "TotalFrames": unpacked_params[7],
            # }
        }
    if apid==70 or apid==71:
        param_part = data[:16]
        # Step 2: Decode the remaining 16 bytes (big-endian)
        unpacked_params = struct.unpack(PARAMS_16_FORMAT_RAW, param_part)

        metadata = {
            "Params": {
                "Width": unpacked_params[0],
                "Height": unpacked_params[1],
                "TDIMode": unpacked_params[2],
                "TDIStages": unpacked_params[3],
                "Binning": unpacked_params[4],
                "LinesPerPacket": unpacked_params[5],
                "Overlap": unpacked_params[6],
                "TotalFrames": unpacked_params[7],
            }
        }
    return metadata

