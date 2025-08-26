import os
import sys

FRAME_LEN = 1404  # Length of a CCSDS Space Packet Protocol frame
HDR_LEN = 6
IDLE_FHP = 2046  # Idle frame indicator
CONTINUATION_FHP = 2047  # Continuation frame indicator
IDLE_BYTE = 0xA5
SECONDARY_SIZE_DEFAULT = 4
STREAM1_VCIDS = {0, 1}
STREAM2_VCIDS = {4, 5}

# --- New Flag ---
MICRO_SAVE_MODE = False # Set to True to save each packet as a separate file, False for combined saving
# ----------------


def decode_primary_header(header):
    """
    Decodes the primary header of a CCSDS Space Packet Protocol frame.

    Args:
        header (bytes): A 6-byte bytearray representing the primary header.

    Returns:
        dict: A dictionary containing decoded header fields.
    """
    # Decode word0: version, mcid, vcid, ocf
    word0 = header[0] | (header[1] << 8)  # LE decode
    version = (word0 >> 14) & 0x03        # bits 15–14
    mcid = (word0 >> 4) & 0x03FF          # bits 13–4
    vcid = (word0 >> 1) & 0x07            # bits 3–1
    ocf_flag = word0 & 0x01               # bit 0

    # Counters
    mcid_counter = header[2]
    vcid_counter = header[3]

    # Decode word2: SHF (bit 15), sync (bit 14), packet order (bit 13), segLen (bits 12–11), FHP (bits 10–0)
    word2 = header[4] | (header[5] << 8)  # LE decode
    secondary_header_flag = (word2 >> 15) & 0x01      # bit 15
    sync_flag = (word2 >> 14) & 0x01                  # bit 14
    packet_order_flag = (word2 >> 13) & 0x01          # bit 13
    segment_length = (word2 >> 11) & 0x03             # bits 12–11
    fhp = word2 & 0x07FF                              # bits 10–0

    return {
        "Version": version,
        "MCID": mcid,
        "VCID": vcid,
        "OCF Flag": ocf_flag,
        "MCID Counter": mcid_counter,
        "VCID Counter": vcid_counter,
        "Secondary Header Flag": secondary_header_flag,
        "Sync Flag": sync_flag,
        "Packet Order Flag": packet_order_flag,
        "Segment Length": segment_length,
        "FHP": fhp
    }


def write_buffer_to_file(buffer_data, vcid, file_identifier, micro_mode=False):
    """
    Writes the given buffer data to a file.

    Args:
        buffer_data (bytearray): The data to write.
        vcid (int): The VCID associated with the data (for filename context).
        file_identifier (int): A numerical identifier for the filename.
        micro_mode (bool): If True, uses 'packet_' prefix; otherwise, 'decoded_packets_part'.
    """
    stream_id = 1 if vcid in STREAM1_VCIDS else 2
    if micro_mode:
        filename = f"packet_{file_identifier}.pkt"
    else:
        filename = f"decoded_packets_part{file_identifier}.pkt"
    with open(filename, "wb") as out:
        out.write(buffer_data)
    print(f"✅ Wrote {len(buffer_data)} bytes to {filename}")


def create_idle_frame(vcid, vcid_counter):
    """
    Creates an idle frame with a specific VCID and VCID counter.

    Args:
        vcid (int): The Virtual Channel ID.
        vcid_counter (int): The Virtual Channel ID counter.

    Returns:
        bytearray: A complete idle frame.
    """
    header = bytearray(6)
    header[0] = 0x00
    header[1] = (vcid << 1) & 0x0E
    header[2] = 0x00
    header[3] = vcid_counter
    # SHF=0, Sync=0, PacketOrder=0, SegLen=3 (idle), FHP=IDLE_FHP
    header[4] = (0 << 7) | (0 << 6) | (0 << 5) | (3 << 3) | ((IDLE_FHP >> 8) & 0x07)
    header[5] = IDLE_FHP & 0xFF
    body = bytes([IDLE_BYTE] * (FRAME_LEN - HDR_LEN))
    return header + body


def _process_frame_logic(frame, current_buffer_obj, current_flag_obj):
    """
    Internal helper function to process a single frame's logic, updating buffer and flag.
    This function modifies `current_buffer_obj` in place.

    Args:
        frame (bytes): The current frame data.
        current_buffer_obj (bytearray): The bytearray holding the current packet data.
        current_flag_obj (bool): True if a packet is currently being assembled, False otherwise.

    Returns:
        tuple: (updated_buffer_obj, updated_flag_obj, saved_packet_data)
               saved_packet_data is a copy of the completed packet if one was finished, else None.
    """
    header = frame[:HDR_LEN]
    decoded = decode_primary_header(header)

    fhp = decoded["FHP"]
    secondary_header_flag = decoded["Secondary Header Flag"]
    segment_length = decoded["Segment Length"]

    secondary_size = 4 if secondary_header_flag else 0
    body = frame[HDR_LEN:]

    saved_packet_data = None  # To return the packet that was completed and should be saved

    # 1. Idle frame
    if fhp == IDLE_FHP:
        print("🛑 Idle Frame")
        if current_buffer_obj:  # If there's a packet currently being assembled
            saved_packet_data = current_buffer_obj[:]  # Mark this for saving
            current_buffer_obj.clear()  # Clear the buffer as the packet is now complete
        return current_buffer_obj, False, saved_packet_data  # Return new flag state

    # 2. Continuation frame
    if fhp == CONTINUATION_FHP:
        current_buffer_obj.extend(body[secondary_size:])
        return current_buffer_obj, current_flag_obj, saved_packet_data

    # 3. Frame with start of new packets (fhp < CONTINUATION_FHP)
    if fhp < CONTINUATION_FHP:
        if current_flag_obj:  # If a packet was already being assembled
            print("🧩 Completing previous packet")
            current_buffer_obj.extend(body[secondary_size:fhp])  # Complete the current packet
            saved_packet_data = current_buffer_obj[:]  # Mark this for saving
            current_buffer_obj.clear()  # Clear buffer for the new packet
        
        # A new packet has started in this frame
        new_flag = True
        if segment_length != 2:  # If it's not a segment end, start buffering the new packet from FHP
            current_buffer_obj.extend(body[fhp:])
        return current_buffer_obj, new_flag, saved_packet_data

    # This line should ideally not be reached if all FHP cases are covered
    return current_buffer_obj, current_flag_obj, saved_packet_data


def process_stream(file_path):
    """
    Processes a stream of CCSDS frames from a file, handling packet assembly,
    frame loss detection, and saving packets based on the MICRO_SAVE_MODE flag.

    Args:
        file_path (str): The path to the input binary file.
    """
    print(f"🔍 Processing: {file_path}")
    last_vcid_counter = {}
    current_buffer = bytearray()  # This is the main buffer for the current packet
    all_buffers = []  # This will only be used if MICRO_SAVE_MODE is False
    out_file_index = 1  # For non-micro mode, combines all_buffers into parts
    packet_index = 0  # For micro mode, for individual packets
    current_flag = False  # Indicates if a packet is currently being assembled

    with open(file_path, "rb") as f:
        frame_number = 0
        while True:
            frame = f.read(FRAME_LEN)
            if not frame or len(frame) < FRAME_LEN:
                break
            frame_number += 1

            header = frame[:HDR_LEN]
            decoded = decode_primary_header(header)

            # print(f"\n🧾 Frame #{frame_number}")
            # for key, value in decoded.items():
            #     print(f"  {key}: {value}")

            vcid = decoded["VCID"]
            fhp = decoded["FHP"]
            vcid_counter = decoded["VCID Counter"]

            # === Frame loss detection ===
            if vcid in last_vcid_counter:
                expected = (last_vcid_counter[vcid] + 1) % 256
                if vcid_counter != expected:
                    print(f"⚠️ Frame loss detected on VCID {vcid}: expected {expected}, got {vcid_counter}")

                    while expected != vcid_counter:
                        idle_frame = create_idle_frame(vcid, expected)
                        print(f"⚙️  Inserting idle frame for VCID {vcid}, VCID Counter {expected}")
                        
                        # Process the inserted idle frame using the common logic
                        current_buffer, current_flag, saved_data = \
                            _process_frame_logic(idle_frame, current_buffer, current_flag)
                        
                        if saved_data:  # If an inserted frame completed a packet
                            if MICRO_SAVE_MODE:
                                packet_index += 1
                                write_buffer_to_file(saved_data, vcid, packet_index, micro_mode=True)
                            else:
                                all_buffers.append(saved_data)
                        
                        # Special handling for IDLE_FHP in non-micro mode for inserted frames
                        # This ensures that segments ending with an inserted idle frame are written
                        if fhp == IDLE_FHP and not MICRO_SAVE_MODE and all_buffers:
                            combined = bytearray()
                            for b in all_buffers:
                                combined.extend(b)
                            write_buffer_to_file(combined, vcid, out_file_index)
                            out_file_index += 1
                            all_buffers.clear()

                        expected = (expected + 1) % 256

            last_vcid_counter[vcid] = vcid_counter

            # Process the current real frame using the common logic
            current_buffer, current_flag, saved_data = \
                _process_frame_logic(frame, current_buffer, current_flag)
            
            if saved_data:  # If the current real frame completed a packet
                if MICRO_SAVE_MODE:
                    packet_index += 1
                    write_buffer_to_file(saved_data, vcid, packet_index, micro_mode=True)
                else:
                    all_buffers.append(saved_data)

            # Special handling for IDLE_FHP in non-micro mode to write combined parts
            # This is for real frames that are IDLE_FHP
            if fhp == IDLE_FHP and not MICRO_SAVE_MODE and all_buffers:
                combined = bytearray()
                for b in all_buffers:
                    combined.extend(b)
                write_buffer_to_file(combined, vcid, out_file_index)
                out_file_index += 1
                all_buffers.clear()

        # After the loop, handle any remaining data in buffer or all_buffers
        if current_buffer:  # If there's a packet still in buffer at EOF
            if MICRO_SAVE_MODE:
                packet_index += 1
                write_buffer_to_file(current_buffer, vcid, packet_index, micro_mode=True)
            else:
                all_buffers.append(current_buffer[:])  # Add to all_buffers for final combined write
            current_buffer.clear()

        if not MICRO_SAVE_MODE and all_buffers:  # Final combined write for non-micro mode
            combined = bytearray()
            for b in all_buffers:
                combined.extend(b)
            write_buffer_to_file(combined, vcid, out_file_index)
            all_buffers.clear()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python decode_packets.py <input_file_path>")
        sys.exit(1)

    input_file = sys.argv[1]
    
    if not os.path.isfile(input_file):
        print(f"❌ File not found: {input_file}")
        sys.exit(1)

    process_stream(input_file)
