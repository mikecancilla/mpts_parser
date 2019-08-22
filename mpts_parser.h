/*
    Original code by Mike Cancilla (https://github.com/mikecancilla)
    2019

    This software is provided 'as-is', without any express or implied
    warranty. In no event will the authors be held liable for any
    damages arising from the use of this software.

    Permission is granted to anyone to use this software for any
    purpose, including commercial applications, and to alter it and
    redistribute it freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must
    not claim that you wrote the original software. If you use this
    software in a product, an acknowledgment in the product documentation
    would be appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and
    must not be misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
    distribution.
*/

#pragma once

#include <vector>
#include <map>
#include <cstdint>

// Type definitions

/*
Taken from: http://www.sno.phy.queensu.ca/~phil/exiftool/TagNames/M2TS.html

M2TS StreamType Values

Value	StreamType	                        Value	StreamType
0x0 = Reserved	                            0x12 = MPEG - 4 generic
0x1 = MPEG - 1 Video	                    0x13 = ISO 14496 - 1 SL - packetized
0x2 = MPEG - 2 Video	                    0x14 = ISO 13818 - 6 Synchronized Download Protocol
0x3 = MPEG - 1 Audio	                    0x1b = H.264 Video
0x4 = MPEG - 2 Audio	                    0x80 = DigiCipher II Video
0x5 = ISO 13818 - 1 private sections        0x81 = A52 / AC - 3 Audio
0x6 = ISO 13818 - 1 PES private data	    0x82 = HDMV DTS Audio
0x7 = ISO 13522 MHEG	                    0x83 = LPCM Audio
0x8 = ISO 13818 - 1 DSM - CC                0x84 = SDDS Audio
0x9 = ISO 13818 - 1 auxiliary               0x85 = ATSC Program ID
0xa = ISO 13818 - 6 multi - protocol encap	0x86 = DTS - HD Audio
0xb = ISO 13818 - 6 DSM - CC U - N msgs     0x87 = E - AC - 3 Audio
0xc = ISO 13818 - 6 stream descriptors      0x8a = DTS Audio
0xd = ISO 13818 - 6 sections                0x91 = A52b / AC - 3 Audio
0xe = ISO 13818 - 1 auxiliary               0x92 = DVD_SPU vls Subtitle
0xf = MPEG - 2 AAC Audio                    0x94 = SDDS Audio
0x10 = MPEG - 4 Video                       0xa0 = MSCODEC Video
0x11 = MPEG - 4 LATM AAC Audio              0xea = Private ES(VC - 1)
*/
enum mpts_e_stream_type
{
    eReserved                                   = 0x0, 
    eMPEG1_Video                                = 0x1, 
    eMPEG2_Video                                = 0x2, 
    eMPEG1_Audio                                = 0x3, 
    eMPEG2_Audio                                = 0x4, 
    eISO13818_1_private_sections                = 0x5, 
    eISO13818_1_PES_private_data                = 0x6, 
    eISO13522_MHEG                              = 0x7, 
    eISO13818_1_DSM_CC                          = 0x8, 
    eISO13818_1_auxiliary                       = 0x9, 
    eISO13818_6_multi_protocol_encap            = 0xa, 
    eISO13818_6_DSM_CC_UN_msgs                  = 0xb, 
    eISO13818_6_stream_descriptors              = 0xc, 
    eISO13818_6_sections                        = 0xd, 
    eISO13818_1_auxiliary2                      = 0xe, 
    eMPEG2_AAC_Audio                            = 0xf, 
    eMPEG4_Video                                = 0x10,
    eMPEG4_LATM_AAC_Audio                       = 0x11,
    eMPEG4_generic                              = 0x12,
    eISO14496_1_SL_packetized                   = 0x13,
    eISO13818_6_Synchronized_Download_Protocol  = 0x14,
    eH264_Video                                 = 0x1b,
    eDigiCipher_II_Video                        = 0x80,
    eA52_AC3_Audio                              = 0x81,
    eHDMV_DTS_Audio                             = 0x82,
    eLPCM_Audio                                 = 0x83,
    eSDDS_Audio                                 = 0x84,
    eATSC_Program_ID                            = 0x85,
    eDTSHD_Audio                                = 0x86,
    eEAC3_Audio                                 = 0x87,
    eDTS_Audio                                  = 0x8a,
    eA52b_AC3_Audio                             = 0x91,
    eDVD_SPU_vls_Subtitle                       = 0x92,
    eSDDS_Audio2                                = 0x94,
    eMSCODEC_Video                              = 0xa0,
    ePrivate_ES_VC1                             = 0xea
};

enum mpts_e_stream_id
{
    program_stream_map = 0xBC,
    private_stream_1 = 0xBD,
    padding_stream = 0xBE,
    private_stream_2 = 0xBF,
    // 110x xxxx = 0xCxxxx, 0xDxxxx = ISO/IEC 13818-3 or ISO/IEC 11172-3 or ISO/IEC 13818-7 or ISO/IEC 14496-3 audio stream number x xxxx
    // 1110 xxxx = 0xExxxx = ITU-T Rec. H.262 | ISO/IEC 13818-2, ISO/IEC 11172-2, ISO/IEC 14496-2 or ITU-T Rec. H.264 | ISO/IEC 14496-10 video stream number xxxx
    ECM_stream = 0xF0,
    EMM_stream = 0xF1,
    DSMCC_stream = 0xF2,                // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Annex A or ISO/IEC 13818-6_DSMCC_stream
    iso_13522_stream = 0xF3,            // ISO/IEC_13522_stream
    itu_h222_a_stream = 0xF4,           // ITU-T Rec. H.222.1 type A
    itu_h222_b_stream = 0xF5,           // ITU-T Rec. H.222.1 type B
    itu_h222_c_stream = 0xF6,           // ITU-T Rec. H.222.1 type C
    itu_h222_d_stream = 0xF7,           // ITU-T Rec. H.222.1 type D
    itu_h222_e_stream = 0xF8,           // ITU-T Rec. H.222.1 type E
    ancillary_stream = 0xF9,
    iso_14496_1_sl_stream = 0xFA,       // ISO/IEC 14496-1_SL-packetized_stream
    iso_14496_1_flex_mux_stream = 0xFB, // ISO/IEC 14496-1_SL-packetized_stream
    metadata_stream = 0xFC,
    extended_stream_id = 0xFD,
    reserved_data_stream = 0xFE,
    program_stream_directory = 0xFF
};

struct mpts_pid_entry_type
{
    std::string pid_name;
    unsigned int num_packets;
    int64_t pid_byte_location;

    mpts_pid_entry_type(std::string pid_name, unsigned int num_packets, int64_t pid_byte_location)
        : pid_name(pid_name)
        , num_packets(num_packets)
        , pid_byte_location(pid_byte_location)
    {
    }
};

typedef std::vector<mpts_pid_entry_type> mpts_pid_list_type;

struct mpts_frame
{
    int pid;
    int frameNumber;
    int totalPackets;
    mpts_pid_list_type pidList;
    mpts_e_stream_type streamType;

    mpts_frame()
        : pid(-1)
        , frameNumber(0)
        , totalPackets(0)
        , streamType(eReserved)
    {}
};

class mpts_parser
{
public:
    mpts_parser(size_t &file_position);
    ~mpts_parser();

    int determine_packet_size(uint8_t buffer[5]);

    int16_t read_pat(uint8_t *&p, bool payload_unit_start);
    int16_t read_pmt(uint8_t *&p, bool payload_unit_start);
    size_t read_descriptors(uint8_t *p, uint16_t program_info_length);

    size_t process_PES_packet_header(uint8_t *&p);
    size_t process_PES_packet(uint8_t *&p, int64_t packet_start_in_file, mpts_e_stream_type stream_type, bool payload_unit_start);
    int16_t process_pid(uint16_t pid, uint8_t *&p, int64_t packet_start_in_file, size_t packet_num, bool payload_unit_start);
    uint8_t process_adaptation_field(unsigned int indent, uint8_t *&p);
    int16_t process_packet(uint8_t *packet, size_t packetNum);
    size_t process_video_frames(uint8_t *p, size_t PES_packet_data_length, unsigned int frames_wanted, unsigned int &frames_received, bool b_xml_out);

    size_t push_video_data(uint8_t *p, size_t size);
    size_t pop_video_data();
    size_t compact_video_data(size_t bytes_to_compact);
    size_t get_video_data_size();

    bool set_print_xml(bool tf);
    bool get_print_xml();

    bool set_terse(bool tf);
    bool get_terse();

    bool set_analyze_elementary_stream(bool tf);
    bool get_analyze_elementary_stream();

private:

    void inline printf_xml(unsigned int indent_level, const char *format, ...);
    void inline inc_ptr(uint8_t *&p, size_t bytes);
    void init_stream_types(std::map <uint16_t, char *> &stream_map);

    uint64_t read_time_stamp(uint8_t *&p);
    float convert_time_stamp(uint64_t time_stamp);

    uint8_t *m_p_video_data;
    size_t &m_file_position;
    unsigned int m_packet_size;
    int16_t m_program_number;
    int16_t m_program_map_pid;
    int16_t m_network_pid; // TODO: this is stored but not used
    int16_t m_scte35_pid; // TODO: this is stored but not used
    size_t m_video_data_size;
    size_t m_video_buffer_size;

    std::map <uint16_t, char *>      m_pid_map; // ID, name
    std::map <uint16_t, mpts_e_stream_type> m_pid_to_type_map; // PID, stream type

    bool m_b_xml;
    bool m_b_terse;
    bool m_b_analyze_elementary_stream;
};
