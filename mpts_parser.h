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

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <any>
#include <cstdint>
#include <base_parser.h>
#include "mpts_descriptors.h"
#include "util.h"

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
enum eMptsStreamType
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

enum eMptsStreamId
{
    program_stream_map = 0xBC,
    private_stream_1 = 0xBD,
    padding_stream = 0xBE,
    private_stream_2 = 0xBF,
    // 110x xxxx = 0xCxxxx, 0xDxxxx = ISO/IEC 13818-3 or ISO/IEC 11172-3 or ISO/IEC 13818-7 or ISO/IEC 14496-3 audio stream number x xxxx
    // 1110 xxxx = 0xExxxx = Rec. ITU-T H.262 | ISO/IEC 13818-2 (MPEG2), ISO/IEC 11172-2, ISO/IEC 14496-2, Rec. ITU-T H.264 | ISO/IEC 14496-10 or Rec. ITU-T H.265 | ISO/IEC 23008-2 video stream number 'xxxx'
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

// Process each PID (Packet Identifier) for each 188 byte packet
//
// Table 2-3 – PID table
/*
    Value           Description
    -----           -----------
    0x0000          Program Association Table
    0x0001          Conditional Access Table
    0x0002          Transport Stream Description Table
    0x0003          IPMP Control Information Table
    0x0004-0x000F   Reserved
    0x0010-0x1FFE   May be assigned as network_PID, Program_map_PID, elementary_PID, or for other purposes
    0x1FFF          Null packet
    NOTE – The transport packets with PID values 0x0000, 0x0001, and 0x0010-0x1FFE are allowed to carry a PCR.
*/

// https://en.wikipedia.org/wiki/MPEG_transport_stream#Packet_identifier_(PID)
// https://www.linuxtv.org/wiki/index.php/PID
/*
Packet identifiers in use
Decimal	    Hexadecimal	    Description
0	        0x0000	        Program association table (PAT) contains a directory listing of all program map tables
1	        0x0001	        Conditional access table (CAT) contains a directory listing of all ITU-T Rec. H.222 entitlement management message streams used by program map tables
2	        0x0002	        Transport stream description table (TSDT) contains descriptors relating to the overall transport stream
3	        0x0003	        IPMP control information table contains a directory listing of all ISO/IEC 14496-13 control streams used by program map tables
4–15	    0x0004-0x000F	Reserved for future use
-----------------------
16–31	    0x0010-0x001F	Used by DVB metadata[10]
            0x0010: NIT, ST
            0x0011: SDT, BAT, ST
            0x0012: EIT, ST, CIT
            0x0013: RST, ST
            0x0014: TDT, TOT, ST
            0x0015: network synchronization
            0x0016: RNT
            0x0017-0x001B: reserved for future use
            0x001C: inband signalling
            0x001D: measurement
            0x001E: DIT
            0x001F: SIT
-----------------------
32-8186	    0x0020-0x1FFA	May be assigned as needed to program map tables, elementary streams and other data tables
8187	    0x1FFB	Used by DigiCipher 2/ATSC MGT metadata
8188–8190	0x1FFC-0x1FFE	May be assigned as needed to program map tables, elementary streams and other data tables
8191	    0x1FFF	        Null Packet (used for fixed bandwidth padding)
*/

enum eMptsPacketIdentifier
{
    ePAT = 0x00,
    eCAT = 0x01,
    eTSDT = 0x02,
    eIPMP = 0x03,
    eReserved0Start = 0x04,
    eReserved0End = 0x0F,
    eNIT = 0x10, // DVB Metadata start, included the as needed set defined below
    eSDT = 0x11,
    eEIT = 0x12,
    eRST = 0x13,
    eTDT = 0x14,
    eNetworkSync = 0x15,
    eRNT = 0x16,
    eReserved1Start = 0x17,
    eReserved1End = 0x1B,
    eInbandSignalling = 0x1C,
    eMeasurement = 0x1D,
    eDIT = 0x1E,
    eSIT = 0x01F, // DVB Metadata end
    eAsNeededStart = 0x10,
    eAsNeededEnd = 0x1FFE,
    eDigiCipher = 0x1FFB,
    eNull = 0x1FFF
};

struct mptsPidEntryType
{
    std::string pidName;
    unsigned int numPackets;
    int64_t pidByteLocation;

    mptsPidEntryType(std::string pidName, unsigned int numPackets, int64_t pidByteLocation)
        : pidName(pidName)
        , numPackets(numPackets)
        , pidByteLocation(pidByteLocation)
    {
    }
};

typedef std::vector<mptsPidEntryType> mptsPidListType;

struct mpts_frame
{
    int pid;
    unsigned int frameNumber;
    int totalPackets;
    mptsPidListType pidList;
    eMptsStreamType streamType;

    mpts_frame()
        : pid(-1)
        , frameNumber(0)
        , totalPackets(0)
        , streamType(eReserved)
    {}
};

// Table 2-30 – Program association section
struct program_pid
{
    program_pid(uint16_t program_number, uint16_t PID)
        : program_number(program_number)
        , PID(PID) {}

    uint16_t program_number;
    uint16_t PID;
};

// Table 2-30 – Program association section
struct program_association_table
{
    bool payload_unit_start;
    uint8_t payload_start_offset;
    uint8_t table_id;
    uint8_t section_syntax_indicator;
    uint16_t section_length;
    uint16_t transport_stream_id;
    uint8_t version_number;
    uint8_t current_next_indicator;
    uint8_t section_number;
    uint8_t last_section_number;
    std::vector<program_pid> program_numbers;
};

// Table 2-33 – Transport stream program map section
struct program_element
{
    program_element(uint8_t stream_type, uint16_t elementary_pid, uint16_t es_info_length)
        : stream_type(stream_type)
        , elementary_pid(elementary_pid)
        , es_info_length(es_info_length) {}

    uint8_t stream_type;
    uint16_t elementary_pid;
    uint16_t es_info_length;
};

struct program_element_descriptor
{
    program_element_descriptor()
        : descriptor_tag(0)
        , descriptor_length(0)
    {}

    program_element_descriptor(const program_element_descriptor& other)
        : descriptor_tag(other.descriptor_tag)
        , descriptor_length(other.descriptor_length)
        , descriptor(other.descriptor)
    {
    }

    void reset()
    {
        descriptor_tag = 0;
        descriptor_length = 0;
        descriptor.reset();
    }

    uint8_t descriptor_tag;
    uint8_t descriptor_length;
    std::shared_ptr<mpts_descriptor> descriptor;
};

// Table 2-33 – Transport stream program map section
struct program_map_table
{
    bool payload_unit_start;
    uint8_t payload_start_offset;
    uint8_t table_id;
    uint8_t section_syntax_indicator;
    uint16_t section_length;
    uint16_t program_number;
    uint8_t version_number;
    uint8_t current_next_indicator;
    uint8_t section_number;
    uint8_t last_section_number;
    uint16_t pcr_pid;
    uint16_t program_info_length;
    std::vector<program_element_descriptor> program_element_descriptors;
    std::vector<program_element> program_elements;
};

struct PES_packet
{
    uint32_t packet_start_code_prefix;
    uint8_t stream_id;

    /* MPTS spec - 2.4.3.7
      PES_packet_length  A 16-bit field specifying the number of bytes in the PES packet following the last byte of the field.
      A value of 0 indicates that the PES packet length is neither specified nor bounded and is allowed only in
      PES packets whose payload consists of bytes from a video elementary stream contained in Transport Stream packets.
    */

    int64_t PES_packet_length;
    uint8_t PES_scrambling_control;
    uint8_t PES_priority;
    uint8_t data_alignment_indicator;
    uint8_t copyright;
    uint8_t original_or_copy;
    uint8_t PTS_DTS_flags;
    uint8_t ESCR_flag;
    uint8_t ES_rate_flag;
    uint8_t DSM_trick_mode_flag;
    uint8_t additional_copy_info_flag;
    uint8_t PES_CRC_flag;
    uint8_t PES_extension_flag;
    /*
        PES_header_data_length  An 8-bit field specifying the total number of bytes occupied by the optional fields and any
        stuffing bytes contained in this PES packet header. The presence of optional fields is indicated in the byte that precedes
        the PES_header_data_length field.
    */
    uint8_t PES_header_data_length;
    uint64_t PTS;
    uint64_t DTS;
    uint32_t ESCR_base;
    uint32_t ESCR_extension;
    uint32_t ES_rate;
    uint8_t trick_mode_control;
    uint8_t field_id;
    uint8_t intra_slice_refresh;
    uint8_t frequency_truncation;
    uint8_t rep_cntrl;
    uint8_t additional_copy_info;
    uint16_t previous_PES_packet_CRC;
    uint8_t PES_private_data_flag;
    uint8_t pack_header_field_flag;
    uint8_t program_packet_sequence_counter_flag;
    uint8_t P_STD_buffer_flag;
    uint8_t PES_extension_flag_2;
    uint8_t PES_private_data[16];
    uint8_t pack_field_length;
    uint8_t program_packet_sequence_counter;
    uint8_t MPEG1_MPEG2_identifier;
    uint8_t original_stuff_length;
    uint8_t P_STD_buffer_scale;
    uint8_t P_STD_buffer_size;
    uint8_t PES_extension_field_length;
    uint8_t stream_id_extension_flag;
    uint8_t stream_id_extension;
    uint8_t tref_extension_flag;
    uint32_t TREF;

    /*
        From the TS spec:
        stuffing_byte  This is a fixed 8-bit value equal to '1111 1111' that can be inserted by the encoder, for example to meet
        the requirements of the channel. It is discarded by the decoder. No more than 32 stuffing bytes shall be present in one
        PES packet header.
    */
};

class mptsParser
{
public:
    mptsParser(size_t &filePosition);
    ~mptsParser();

    int determine_packet_size(uint8_t buffer[5]);

    size_t readPAT(uint8_t *&p, bool payloadUnitStart);
    size_t readPAT(uint8_t*& p, program_association_table& pat, bool payloadUnitStart);
    size_t readPMT(uint8_t *&p, bool payloadUnitStart);
    size_t readPMT(uint8_t*& p, program_map_table& pmt, bool payloadUnitStart);
    size_t readElementDescriptors(uint8_t* p, program_map_table& pmt);
    size_t readElementDescriptors(uint8_t* p, uint16_t programInfoLength);

    size_t processPESPacketHeader(uint8_t*& p, size_t PESPacketDataLength, PES_packet& pes_packet);
    size_t processPESPacketHeader(uint8_t *&p, size_t PESPacketDataLength);
    size_t processPESPacket(uint8_t *&packetStart, uint8_t *&p, eMptsStreamType streamType, bool payloadUnitStart);
    int16_t processPid(uint16_t pid, uint8_t *&packetStart, uint8_t *&p, int64_t packetStartInFile, size_t packetNum, bool payloadUnitStart, uint8_t adaptationFieldLength);
    uint8_t getAdaptationFieldLength(uint8_t *&p);
    uint8_t processAdaptationField(unsigned int indent, uint8_t *&p);
    int16_t processPacket(uint8_t *packet, size_t packetNum);
    size_t processVideoFrames(uint8_t* p,
        size_t PESPacketDataLength,
        eMptsStreamType streamType,
        unsigned int& frameNumber, // Will be incremented by 1 per parsed frame
        unsigned int framesWanted,
        unsigned int& framesReceived);
    size_t processVideoFrames(uint8_t* p,
        size_t PESPacketDataLength,
        mpts_frame* pFrame);

    size_t pushVideoData(uint8_t *p, size_t size);
    size_t popVideoData();
    size_t compactVideoData(size_t bytesToCompact);
    size_t getVideoDataSize();

    void printFrameInfo(mpts_frame *pFrame);
    void printElementDescriptors(const program_map_table& pmt);

    bool setTerse(bool tf);
    bool getTerse();

    bool setAnalyzeElementaryStream(bool tf);
    bool getAnalyzeElementaryStream();

    void flush();

private:

    template<typename... Args>
    void printfXml(int indentLevel, Args... args)
    {
        util::printfXml(indentLevel, args...);
    }

    void inline incPtr(uint8_t *&p, size_t bytes);
    void initStreamTypes(std::map <uint16_t, const char *> &streamMap);

    uint64_t readTimeStamp(uint8_t *&p);
    float convertTimeStamp(uint64_t timeStamp);

    uint8_t *m_pVideoData;
    size_t &m_filePosition;
    unsigned int m_packetSize;
    int16_t m_programNumber;
    int16_t m_programMapPid;
    int16_t m_networkPid; // TODO: this is stored but not used
    int16_t m_scte35Pid; // TODO: this is stored but not used
    size_t m_videoDataSize;
    size_t m_videoBufferSize;

    std::map <uint16_t, const char *> m_pidToNameMap; // ID, name
    std::map <uint16_t, eMptsStreamType> m_pidToTypeMap; // PID, stream type

    bool m_bTerse;
    bool m_bAnalyzeElementaryStream;

    mpts_frame m_videoFrame;
    mpts_frame m_audioFrame;

    std::shared_ptr<baseParser> m_parser;
};
