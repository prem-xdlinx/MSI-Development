import struct
import os
from metadata_decoderLittle import decode_metadata_chunks, decode_Params_Band, decode_ephemeris_struct

# Mapping APID values to their respective file extensions
APID_EXTENSIONS = {
    0: ".band00",
    1: ".band01",
    2: ".band02",
    10: ".band10",
    11: ".band11",
    12: ".band12",
    20: ".band20",
    21: ".band21",
    22: ".band22",
    30: ".band30",
    31: ".band31",
    32: ".band32",
    40: ".band40",
    41: ".band41",
    42: ".band42",
    50: ".band50",
    51: ".band51",
    52: ".band52",
    60: ".band60",
    61: ".band61",
    62: ".band62",
    70: ".raw0",
    71: ".raw1",
    72: ".log",
}

# Define CCSDS Header Format
HEADER_SIZE = 6
OUTPUT_DIR = "DecodeSoftware"
EphemerisParts = 5
    
def parse_ccsds_header(header_bytes):
    """Parses the 6-byte CCSDS header and returns extracted values."""
    """
    # Unpack the first two bytes (16-bit)
    first_word = struct.unpack(">H", header_bytes[0:2])[0]
    version = (first_word >> 13) & 0b111
    packet_type = (first_word >> 12) & 0b1
    sec_header_flag = (first_word >> 11) & 0b1
    app_id = first_word & 0x07FF  # Lower 11 bits

    # Unpack the second two bytes (16-bit)
    second_word = struct.unpack(">H", header_bytes[2:4])[0]
    seq_flag = (second_word >> 14) & 0b11
    packet_seq_count = second_word & 0x3FFF  # Lower 14 bits

    # Unpack the last two bytes (16-bit) - Packet Data Length
    data_length = struct.unpack(">H", header_bytes[4:6])[0] + 1  # CCSDS rule: stored as (length - 1)
    # Return all extracted values
    return {
        "Version": version,
        "Packet Type": packet_type,
        "Secondary Header Flag": sec_header_flag,
        "Application ID": app_id,
        "Sequence Flag": seq_flag,
        "Packet Sequence Count": packet_seq_count,
        "Packet Data Length": data_length
    }
    """
    secondary_header = header_bytes[0]  # == 8
    apid = header_bytes[1]              # your APID byte
    seq_control = header_bytes[2] | (header_bytes[3] << 8)
    seq_flag = (seq_control >> 14) & 0x03
    seq_count = seq_control & 0x3FFF
    data_len_field = header_bytes[4]  | (header_bytes[5]<< 8)
    data_length = data_len_field+1   # CCSDS: actual = field + 1

    # print("=== Primary Header ===")
    # print(f"Secondary Header flag: {secondary_header}")
    # print(f"APID:                   {apid}")
    # print(f"Sequence Flag:         {seq_flag}")
    # print(f"Sequence Count:        {seq_count}")
    # print(f"Packet Data Length:    {data_length}")

    return {
        "Secondary Header Flag": secondary_header,
        "Application ID": apid,
        "Sequence Flag": seq_flag,
        "Packet Sequence Count": seq_count,
        "Packet Data Length": data_length
    }

def decode_pkt_file(file_path):
    """Decodes a CCSDS packetized file and writes extracted payloads to a single output file."""
    metaLen = 56
    secondaryLen = 2
    paramLen_Band = 20
    paramLen_Raw = 16
    if not os.path.exists(file_path):
        print(f"Error: File not found - {file_path}")
        return

    with open(file_path, "rb") as file:
        # Read the first packet header to determine APID and output file
        header_bytes = file.read(HEADER_SIZE)
        if not header_bytes or len(header_bytes) < HEADER_SIZE:
            print("Error: File does not contain a valid CCSDS header.")
            return

        header_info = parse_ccsds_header(header_bytes)
        app_id = header_info["Application ID"]
        
        # Determine output file extension based on APID
        file_extension = APID_EXTENSIONS.get(app_id, ".unknown")
        base_filename = os.path.basename(file_path).replace(".pkt", file_extension)
        output_file_path = os.path.join(OUTPUT_DIR, base_filename)
        output_file_path_meta = os.path.join(OUTPUT_DIR, f"{base_filename}.meta")
        output_file_path_ephemeris = os.path.join(OUTPUT_DIR, f"{base_filename}_ephermis.txt")


        # Ensure output directory exists
        os.makedirs(OUTPUT_DIR, exist_ok=True)

        print(f"Decoding {file_path} -> Saving to {output_file_path}")

        # Open the output file in append mode
        with open(output_file_path, "wb") as output_file, open(output_file_path_meta, "w") as meta_file, open(output_file_path_ephemeris, "w") as eph_file:
            def write_meta(header_info, secondary_hdr_len=None, mode_string=None, metadata_info=None, params=None):
                meta_file.write("\n========== Decoded CCSDS Header ==========\n")
                for key, value in header_info.items():
                    meta_file.write(f"{key:25}: {value}\n")
                if secondary_hdr_len is not None:
                    meta_file.write(f"Secondary Header Length     : {secondary_hdr_len}\n")
                if metadata_info is not None:
                    meta_file.write("======== Meta Chunks ========\n")
                    for k, v in metadata_info.items():
                        # if isinstance(v, dict):
                        #     meta_file.write(f"{k}:\n")
                        #     for sub_k, sub_v in v.items():
                        #         meta_file.write(f"  {sub_k:20}: {sub_v}\n")
                        # else:
                        meta_file.write(f"{k:25}: {v}\n")
                    meta_file.write("===========================================\n")

                if params is not None:
                    meta_file.write("======== Params Chunks ========\n")
                    for k, v in params.items():
                        meta_file.write(f"{k:25}: {v}\n")
                    meta_file.write("===========================================\n")

                # Write mode string if it exists
                if mode_string:
                    meta_file.write("=== Mode String ===\n")
                    meta_file.write(f"{mode_string}\n")
                meta_file.write("===========================================\n\n")
            # Write the first header to output file

            print("\n========== Decoded CCSDS Header ==========")
            for key, value in header_info.items():
                print(f"{key:25}: {value}")
            print("===========================================\n")
            # Process the first payload
            data_length = header_info["Packet Data Length"]
            seqCount = header_info["Packet Sequence Count"]
            secondaryStatus = header_info["Secondary Header Flag"]
            apid = header_info["Application ID"]

            payload = file.read(data_length)
            print(f"Packet read size: {len(payload)}")
            if secondaryStatus:
                if len(payload) < secondaryLen:
                    print("Error: Incomplete secondary header length field.")
                    return
            
                secondary_hdr_len = int.from_bytes(payload[0:secondaryLen], byteorder='little')
                print(f"Secondary Header Length: {secondary_hdr_len}")
                data_only = payload[secondary_hdr_len:]
                if apid not in (70, 71, 72):
                    if len(payload) < (secondaryLen + metaLen):
                        print("Error: Not enough data for metadata chunk.")
                        return
                    # metadata_bytes = payload[secondaryLen:secondaryLen+metaLen+paramLen_Band]
                    metadata_bytes = payload[secondaryLen:secondaryLen+metaLen]
                    metadata_info = decode_metadata_chunks(metadata_bytes, apid)
                    offset = secondaryLen + metaLen
                    for i in range(EphemerisParts):
                        if offset + 4 > len(payload):
                            print(f"Error: Not enough data for Ephemeris header {i}")
                            break
                         # Read index
                        idx = int.from_bytes(payload[offset:offset+2], byteorder='little')
                        offset += 2

                        # Read struct size
                        struct_size = int.from_bytes(payload[offset:offset+2], byteorder='little')
                        offset += 2
                        eph_file.write(f"Ephemeris Part {i} Index: {idx}, Size: {struct_size}\n")
                        if offset + struct_size > len(payload):
                            print(f"Error: Not enough data for Ephemeris struct {i}")
                            break

                        struct_bytes = payload[offset:offset+struct_size]
                        offset += struct_size

                        ephemerisInfo = decode_ephemeris_struct(struct_bytes)
                        for key, value in ephemerisInfo.items():
                            eph_file.write(f"{key:25}: {value}\n")
                        eph_file.write("\n")
                        eph_file.write("=============================================================\n")
                    # Call external decoder
                    # Now read params (band) and mode string
                    param_Bytes = payload[offset:offset+paramLen_Band]
                    params_info = decode_Params_Band(param_Bytes)
                    offset += paramLen_Band

                    if offset < len(payload):
                        raw_mode_bytes = payload[offset:secondary_hdr_len]
                        null_terminated_index = raw_mode_bytes.find(b'\x00')
                        if null_terminated_index != -1:
                            mode_string = raw_mode_bytes[:null_terminated_index].decode('ascii', errors='ignore')
                        else:
                            mode_string = raw_mode_bytes.decode('ascii', errors='ignore')
                    else:
                        mode_string = None

                    # Still write everything to output file
                    write_meta(header_info, secondary_hdr_len, mode_string, metadata_info, params_info)
                    
                    output_file.write(data_only)
                # else:
                #     # For APID 70, 71, or 72 — assume no metadata
                #     metadata_bytes = payload[secondaryLen:secondaryLen+paramLen_Raw]

                #     # Call external decoder
                #     metadata_info = decode_metadata_chunks(metadata_bytes, apid)

                #     # Remaining part is mode string (if any)
                #     mode_start = secondaryLen +paramLen_Raw                                 
                #     if mode_start < len(payload):
                #         raw_mode_bytes = payload[mode_start:secondary_hdr_len]
                #         null_terminated_index = raw_mode_bytes.find(b'\x00')  # C-style null termination

                #         if null_terminated_index != -1:
                #             mode_string = raw_mode_bytes[:null_terminated_index].decode('ascii', errors='ignore')
                #         else:
                #             mode_string = raw_mode_bytes.decode('ascii', errors='ignore')  # fallback

                #         print("=== Mode String ===")
                #         print(mode_string)
                #     write_meta(header_info, secondary_hdr_len, mode_string, metadata_info)
                #     output_file.write(data_only)
            else:
                print("No secondary header — write entire payload")
                write_meta(header_info)
                output_file.write(payload)
            prevSeqCount = seqCount
            packet_count = 1

            while True:
                
                header_bytes = file.read(HEADER_SIZE)
                print(f"Header read size: {len(header_bytes)}")
                if not header_bytes or len(header_bytes) < HEADER_SIZE:
                    break  # End of file

                # Parse header
                header_info = parse_ccsds_header(header_bytes)
                # Read next CCSDS header
                print("\n========== Decoded CCSDS Header ==========")
                for key, value in header_info.items():
                    print(f"{key:25}: {value}")
                print("===========================================\n")
                seqCount = header_info["Packet Sequence Count"]
                data_length = header_info["Packet Data Length"]
                secondaryStatus = header_info["Secondary Header Flag"]
                apid = header_info["Application ID"]
                if data_length<=1:
                    break
                if seqCount == 0:
                    prevSeqCount = seqCount-1
                # Check if the sequence count is continuous
                if seqCount != (prevSeqCount + 1):
                    # pass
                    # Fill missing sequence count data with idle bytes (0x00)
                    missing_packets = seqCount - (prevSeqCount + 1)
                    for _ in range(missing_packets):
                        idle_data = bytes(data_length)  # Create idle data (all 0s)
                        output_file.write(idle_data)
                        # print(f"Missing packet detected! Filling {data_length} bytes with idle data (0x00)")
                    

                # Read payload data
                payload = file.read(data_length)
                print(f"Packet read size: {len(payload)}")
                if apid not in (70, 71, 72):
                    if len(payload) < (secondaryLen + metaLen):
                        print("Error: Not enough data for metadata chunk.")
                        return
                    # metadata_bytes = payload[secondaryLen:secondaryLen+metaLen+paramLen_Band]
                    metadata_bytes = payload[secondaryLen:secondaryLen+metaLen]
                    metadata_info = decode_metadata_chunks(metadata_bytes, apid)
                    offset = secondaryLen + metaLen
                    for i in range(EphemerisParts):
                        if offset + 4 > len(payload):
                            print(f"Error: Not enough data for Ephemeris header {i}")
                            break
                         # Read index
                        idx = int.from_bytes(payload[offset:offset+2], byteorder='little')
                        offset += 2

                        # Read struct size
                        struct_size = int.from_bytes(payload[offset:offset+2], byteorder='little')
                        offset += 2
                        eph_file.write(f"Ephemeris Part {i} Index: {idx}, Size: {struct_size}\n")
                        if offset + struct_size > len(payload):
                            print(f"Error: Not enough data for Ephemeris struct {i}")
                            break

                        struct_bytes = payload[offset:offset+struct_size]
                        offset += struct_size

                        ephemerisInfo = decode_ephemeris_struct(struct_bytes)
                        for key, value in ephemerisInfo.items():
                            eph_file.write(f"{key:25}: {value}\n")
                        eph_file.write("\n")
                        eph_file.write("=============================================================\n")
                    # Call external decoder
                    # Now read params (band) and mode string
                    param_Bytes = payload[offset:offset+paramLen_Band]
                    params_info = decode_Params_Band(param_Bytes)
                    offset += paramLen_Band

                    if offset < len(payload):
                        raw_mode_bytes = payload[offset:secondary_hdr_len]
                        null_terminated_index = raw_mode_bytes.find(b'\x00')
                        if null_terminated_index != -1:
                            mode_string = raw_mode_bytes[:null_terminated_index].decode('ascii', errors='ignore')
                        else:
                            mode_string = raw_mode_bytes.decode('ascii', errors='ignore')
                    else:
                        mode_string = None

                    # Still write everything to output file
                    write_meta(header_info, secondary_hdr_len, mode_string, metadata_info, params_info)
                    
                    output_file.write(data_only)
                # else:
                #     # For APID 70, 71, or 72 — assume no metadata
                #     metadata_bytes = payload[secondaryLen:secondaryLen+paramLen_Raw]

                #     # Call external decoder
                #     metadata_info = decode_metadata_chunks(metadata_bytes, apid)

                #     # Remaining part is mode string (if any)
                #     mode_start = secondaryLen +paramLen_Raw                                 
                #     if mode_start < len(payload):
                #         raw_mode_bytes = payload[mode_start:secondary_hdr_len]
                #         null_terminated_index = raw_mode_bytes.find(b'\x00')  # C-style null termination

                #         if null_terminated_index != -1:
                #             mode_string = raw_mode_bytes[:null_terminated_index].decode('ascii', errors='ignore')
                #         else:
                #             mode_string = raw_mode_bytes.decode('ascii', errors='ignore')  # fallback

                #         print("=== Mode String ===")
                #         print(mode_string)
                #     write_meta(header_info, secondary_hdr_len, mode_string, metadata_info)
                #     output_file.write(data_only)
                else:
                    write_meta(header_info)
                    # No secondary header — write entire payload
                    output_file.write(payload)
                # Append payload to output file
                packet_count += 1
                prevSeqCount = seqCount  # Update previous sequence count
                # if packet_count == 4:
                #     break


        print(f"\nTotal Packets Processed: {packet_count}")

# Example Usage
pkt_file_path = "decoded_packets_part5.pkt"
decode_pkt_file(pkt_file_path)