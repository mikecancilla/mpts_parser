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

/*
    SCTE 35 in MP2TS
    http://www.scte.org/SCTEDocs/Standards/SCTE%2035%202016.pdf
*/

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstdarg>
#include <vector>
#include <map>
#include "tinyxml2.h"

#define READ_2_BYTES(p) *p | (*(p+1) << 8); p+=2;

// Forward function definitions
size_t read_descriptors(uint8_t *p, uint16_t program_info_length);

// Type definitions
enum eStreamType
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

enum eExtensionType
{
    sequence_extension = 0,
    extension_and_user_data_0,
    extension_and_user_data_1,
    extension_and_user_data_2,
    num_extension_types
};

struct pid_entry_type
{
    std::string pid_name;
    unsigned int num_packets;
    int64_t pid_byte_location;

    pid_entry_type(std::string pid_name, unsigned int num_packets, int64_t pid_byte_location)
        : pid_name(pid_name)
        , num_packets(num_packets)
        , pid_byte_location(pid_byte_location)
    {
    }
};

typedef std::vector<pid_entry_type> pid_list_type;

// Global definitions
pid_list_type g_video_pid_list;
pid_list_type g_audio_pid_list;

std::map <uint16_t, char *>      g_stream_map; // ID, name
std::map <uint16_t, char *>      g_pid_map; // ID, name
std::map <uint16_t, eStreamType> g_pid_to_type_map; // PID, stream type

bool g_b_xml =      true;
bool g_b_progress = false;
bool g_b_gui =      false;
bool g_b_debug =    false;
bool g_b_terse =    true;

int16_t g_program_number = -1;
int16_t g_program_map_pid = -1;
int16_t g_network_pid = 0x0010; // default value
int16_t g_scte35_pid = -1;
size_t g_ptr_position = 0;
unsigned int g_packet_size = 0;
eExtensionType g_current_mpeg2_extension_type = sequence_extension;

#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 600

// Program and program element descriptors
//#define 0 n / a n / a Reserved
//#define 1 n / a n / a Reserved
#define VIDEO_STREAM_DESCRIPTOR                 ((uint8_t) 2)
#define AUDIO_STREAM_DESCRIPTOR                 ((uint8_t) 3)
#define HIERARCHY_DESCRIPTOR                    ((uint8_t) 4)
#define REGISTRATION_DESCRIPTOR                 ((uint8_t) 5)
#define DATA_STREAM_ALIGNMENT_DESCRIPTOR        ((uint8_t) 6)
#define TARGET_BACKGROUND_GRID_DESCRIPTOR       ((uint8_t) 7)
#define VIDEO_WINDOW_DESCRIPTOR                 ((uint8_t) 8)
#define CA_DESCRIPTOR                           ((uint8_t) 9)
#define ISO_639_LANGUAGE_DESCRIPTOR             ((uint8_t) 10)
#define SYSTEM_CLOCK_DESCRIPTOR                 ((uint8_t) 11)
#define MULTIPLEX_BUFFER_UTILIZATION_DESCRIPTOR ((uint8_t) 12)
#define COPYRIGHT_DESCRIPTOR                    ((uint8_t) 13)
#define MAXIMUM_BITRATE_DESCRIPTOR              ((uint8_t) 14)
#define PRIVATE_DATA_INDICATOR_DESCRIPTOR       ((uint8_t) 15)
#define SMOOTHING_BUFFER_DESCRIPTOR             ((uint8_t) 16)
#define STD_DESCRIPTOR                          ((uint8_t) 17)
#define IBP_DESCRIPTOR                          ((uint8_t) 18)
//#define 19 - 26 X Defined in ISO / IEC 13818 - 6
#define MPEG_4_VIDEO_DESCRIPTOR                 ((uint8_t) 27)
#define MPEG_4_AUDIO_DESCRIPTOR                 ((uint8_t) 28)
#define IOD_DESCRIPTOR                          ((uint8_t) 29)
#define SL_DESCRIPTOR                           ((uint8_t) 30)
#define FMC_DESCRIPTOR                          ((uint8_t) 31)
#define EXTERNAL_ES_ID_DESCRIPTOR               ((uint8_t) 32)
#define MUXCODE_DESCRIPTOR                      ((uint8_t) 33)
#define FMXBUFFERSIZE_DESCRIPTOR                ((uint8_t) 34)
#define MULTIPLEXBUFFER_DESCRIPTOR              ((uint8_t) 35)
//#define 36 - 63 n / a n / a ITU - T Rec.H.222.0 | ISO / IEC 13818 - 1 Reserved
//#define 64 - 255 n / a n / a User Private

void inline my_printf(const char *format, ...)
{
    if(g_b_debug)
    {
        va_list arg_list;
        va_start(arg_list, format);
        vprintf(format, arg_list);
    }
}

void inline printf_xml(tinyxml2::XMLDocument *doc, const char *elementName, const char *elementText)
{
    if(elementText)
    {
        tinyxml2::XMLElement *element = doc->NewElement(elementName);
        element->SetText(elementText);
//        doc->InsertEndChild(element);
        doc->InsertFirstChild(element);
    }
    else
    {
        tinyxml2::XMLElement *element = doc->NewElement(elementName);
        if(elementName[0] != '/')
            doc->InsertFirstChild(element);
        else
            doc->InsertEndChild(element);
    }

    doc->Print();
}

void inline printf_xml(unsigned int indent_level, const char *format, ...)
{
    if(g_b_xml && format)
    {
        char output_buffer[512] = "";

        for(unsigned int i = 0; i < indent_level; i++)
            strcat_s(output_buffer, sizeof(output_buffer), "  ");

        va_list arg_list;
        va_start(arg_list, format);
        vsprintf_s(output_buffer + (indent_level*2), sizeof(output_buffer) - (indent_level*2), format, arg_list);
        va_end(arg_list);

        printf(output_buffer);
    }
}

void inline inc_ptr(uint8_t *&p, size_t bytes)
{
    p += bytes;
    g_ptr_position += bytes;
}

// Initialize a map of ID to string type
void init_stream_types()
{
    g_stream_map[0x0] = "Reserved";	                            
    g_stream_map[0x1] = "MPEG-1 Video";
    g_stream_map[0x2] = "MPEG-2 Video";
    g_stream_map[0x3] = "MPEG-1 Audio";
    g_stream_map[0x4] = "MPEG-2 Audio";
    g_stream_map[0x5] = "ISO 13818-1 private sections";
    g_stream_map[0x6] = "ISO 13818-1 PES private data";
    g_stream_map[0x7] = "ISO 13522 MHEG";
    g_stream_map[0x8] = "ISO 13818-1 DSM - CC";
    g_stream_map[0x9] = "ISO 13818-1 auxiliary";
    g_stream_map[0xa] = "ISO 13818-6 multi-protocol encap";
    g_stream_map[0xb] = "ISO 13818-6 DSM-CC U-N msgs";
    g_stream_map[0xc] = "ISO 13818-6 stream descriptors";
    g_stream_map[0xd] = "ISO 13818-6 sections";
    g_stream_map[0xe] = "ISO 13818-1 auxiliary";
    g_stream_map[0xf] = "MPEG-2 AAC Audio";
    g_stream_map[0x10] = "MPEG-4 Video";
    g_stream_map[0x11] = "MPEG-4 LATM AAC Audio";
    g_stream_map[0x12] = "MPEG-4 generic";
    g_stream_map[0x13] = "ISO 14496-1 SL-packetized";
    g_stream_map[0x14] = "ISO 13818-6 Synchronized Download Protocol";
    g_stream_map[0x1b] = "H.264 Video";
    g_stream_map[0x80] = "DigiCipher II Video";
    g_stream_map[0x81] = "A52 / AC-3 Audio";
    g_stream_map[0x82] = "HDMV DTS Audio";
    g_stream_map[0x83] = "LPCM Audio";
    g_stream_map[0x84] = "SDDS Audio";
    g_stream_map[0x85] = "ATSC Program ID";
    g_stream_map[0x86] = "DTS-HD Audio";
    g_stream_map[0x87] = "E-AC- 3 Audio";
    g_stream_map[0x8a] = "DTS Audio";
    g_stream_map[0x91] = "A52b / AC-3 Audio";
    g_stream_map[0x92] = "DVD_SPU vls Subtitle";
    g_stream_map[0x94] = "SDDS Audio";
    g_stream_map[0xa0] = "MSCODEC Video";
    g_stream_map[0xea] = "Private ES(VC-1)";
}

inline uint16_t read_2_bytes(uint8_t *p)
{
	uint16_t ret = *p++;
	ret <<= 8;
	ret |= *p++;

	return ret;
}

inline uint32_t read_4_bytes(uint8_t *p)
{
    uint32_t ret = 0;
    uint32_t val = *p++;
    ret = val<<24;
    val = *p++;
    ret |= val << 16;
    val = *p++;
    ret |= val << 8;
    ret |= *p;

    return ret;
}

// 2.4.4.3 Program association Table
//
// The Program Association Table provides the correspondence between a program_number and the PID value of the
// Transport Stream packets which carry the program definition.The program_number is the numeric label associated with
// a program.
int16_t read_pat(uint8_t *&p, bool payload_unit_start)
{
    uint8_t payload_start_offset = 0;

    if(payload_unit_start)
    {
        payload_start_offset = *p; // Spec 2.4.4.1
        inc_ptr(p, 1);
        inc_ptr(p, payload_start_offset);
    }

    uint8_t table_id = *p;
    inc_ptr(p, 1);
    uint16_t section_length = read_2_bytes(p);
    inc_ptr(p, 2);
    uint8_t section_syntax_indicator = (0x8000 & section_length) >> 15;
    section_length &= 0xFFF;

    uint8_t *p_section_start = p;

    uint16_t transport_stream_id = read_2_bytes(p);
    inc_ptr(p, 2);

    uint8_t current_next_indicator = *p;
    inc_ptr(p, 1);
    uint8_t version_number = (current_next_indicator & 0x3E) >> 1;
    current_next_indicator &= 0x1;

    uint8_t section_number = *p;
    inc_ptr(p, 1);
    uint8_t last_section_number = *p;
    inc_ptr(p, 1);

    printf_xml(2, "<program_association_table>\n");
    if(payload_unit_start)
        printf_xml(3, "<pointer_field>0x%x</pointer_field>\n", payload_start_offset);
    printf_xml(3, "<table_id>0x%x</table_id>\n", table_id);
    printf_xml(3, "<section_syntax_indicator>%d</section_syntax_indicator>\n", section_syntax_indicator);
    printf_xml(3, "<section_length>%d</section_length>\n", section_length);
    printf_xml(3, "<transport_stream_id>0x%x</transport_stream_id>\n", transport_stream_id);
    printf_xml(3, "<version_number>0x%x</version_number>\n", version_number);
    printf_xml(3, "<current_next_indicator>0x%x</current_next_indicator>\n", current_next_indicator);
    printf_xml(3, "<section_number>0x%x</section_number>\n", section_number);
    printf_xml(3, "<last_section_number>0x%x</last_section_number>\n", last_section_number);

    while ((p - p_section_start) < (section_length - 4))
    {
        g_program_number = read_2_bytes(p);
        inc_ptr(p, 2);
        uint16_t network_pid = 0;

        if (0 == g_program_number)
        {
            network_pid = read_2_bytes(p);
            inc_ptr(p, 2);
            network_pid &= 0x1FFF;
            g_network_pid = network_pid;
        }
        else
        {
            g_program_map_pid = read_2_bytes(p);
            inc_ptr(p, 2);
            g_program_map_pid &= 0x1FFF;
        }

        printf_xml(3, "<program>\n");
        printf_xml(4, "<number>%d</number>\n", g_program_number);

        if(network_pid)
            printf_xml(4, "<network_pid>0x%x</network_pid>\n", g_network_pid);
        else
            printf_xml(4, "<program_map_pid>0x%x</program_map_pid>\n", g_program_map_pid);

        printf_xml(3, "</program>\n");
    }

    printf_xml(2, "</program_association_table>\n");

    return 0;
}

// 2.4.4.8 Program Map Table
//
// The Program Map Table provides the mappings between program numbers and the program elements that comprise
// them.A single instance of such a mapping is referred to as a "program definition".The program map table is the
// complete collection of all program definitions for a Transport Stream.
int16_t read_pmt(uint8_t *&p, bool payload_unit_start)
{
    uint8_t payload_start_offset = 0;

    if(payload_unit_start)
    {
        payload_start_offset = *p; // Spec 2.4.4.1
        inc_ptr(p, 1);
        inc_ptr(p, payload_start_offset);
    }

    uint8_t table_id = *p;
    inc_ptr(p, 1);
    uint16_t section_length = read_2_bytes(p);
    inc_ptr(p, 2);
    uint8_t section_syntax_indicator = section_length & 0x80 >> 15;
    section_length &= 0xFFF;

    uint8_t *p_section_start = p;

    uint16_t program_number = read_2_bytes(p);
    inc_ptr(p, 2);

    uint8_t current_next_indicator = *p;
    inc_ptr(p, 1);
    uint8_t version_number = (current_next_indicator & 0x3E) >> 1;
    current_next_indicator &= 0x1;

    uint8_t section_number = *p;
    inc_ptr(p, 1);
    uint8_t last_section_number = *p;
    inc_ptr(p, 1);

    uint16_t pcr_pid = read_2_bytes(p);
    inc_ptr(p, 2);
    pcr_pid &= 0x1FFF;

    g_pid_map[pcr_pid] = "PCR";

    uint16_t program_info_length = read_2_bytes(p);
    inc_ptr(p, 2);

    program_info_length &= 0x3FF;
    
    printf_xml(2, "<program_map_table>\n");
    if(payload_unit_start)
        printf_xml(3, "<pointer_field>0x%x</pointer_field>\n", payload_start_offset);
    printf_xml(3, "<table_id>0x%x</table_id>\n", table_id);
    printf_xml(3, "<section_syntax_indicator>%d</section_syntax_indicator>\n", section_syntax_indicator);
    printf_xml(3, "<section_length>%d</section_length>\n", section_length);
    printf_xml(3, "<program_number>%d</program_number>\n", program_number);
    printf_xml(3, "<version_number>%d</version_number>\n", version_number);
    printf_xml(3, "<current_next_indicator>%d</current_next_indicator>\n", current_next_indicator);
    printf_xml(3, "<section_number>%d</section_number>\n", section_number);
    printf_xml(3, "<last_section_number>%d</last_section_number>\n", last_section_number);
    printf_xml(3, "<pcr_pid>0x%x</pcr_pid>\n", pcr_pid);
    printf_xml(3, "<program_info_length>%d</program_info_length>\n", program_info_length);

    p += read_descriptors(p, program_info_length);

    my_printf("program_number:%d, pcr_pid:%x\n", program_number, pcr_pid);
    my_printf("  Elementary Streams:\n");

    size_t stream_count = 0;

    // Subtract 4 from section_length to account for 4 byte CRC at its end.  The CRC is not program data.
    while((p - p_section_start) < (section_length - 4))
    {
        uint8_t stream_type = *p;
        inc_ptr(p, 1);
        uint16_t elementary_pid = read_2_bytes(p);
        inc_ptr(p, 2);
        elementary_pid &= 0x1FFF;

        uint16_t es_info_length = read_2_bytes(p);
        inc_ptr(p, 2);
        es_info_length &= 0xFFF;

        p += es_info_length;

        // Scte35 stream type is 0x86
        if(0x86 == stream_type)
            g_scte35_pid = elementary_pid;

        g_pid_map[elementary_pid] = g_stream_map[stream_type];
        g_pid_to_type_map[elementary_pid] = (eStreamType) stream_type;

        my_printf("    %d) pid:%x, stream_type:%x (%s)\n", stream_count++, elementary_pid, stream_type, g_stream_map[stream_type]);

        printf_xml(3, "<stream>\n");
        printf_xml(4, "<number>%zd</number>\n", stream_count);
        printf_xml(4, "<pid>0x%x</pid>\n", elementary_pid);
        printf_xml(4, "<type_number>0x%x</type_number>\n", stream_type);
        printf_xml(4, "<type_name>%s</type_name>\n", g_stream_map[stream_type]);
        printf_xml(3, "</stream>\n");

        stream_count++;
    }

    printf_xml(2, "</program_map_table>\n");

    return 0;
}

// 2.6 Program and program element descriptors
// Program and program element descriptors are structures which may be used to extend the definitions of programs and
// program elements.All descriptors have a format which begins with an 8 - bit tag value.The tag value is followed by an
// 8 - bit descriptor length and data fields.
size_t read_descriptors(uint8_t *p, uint16_t program_info_length)
{
    uint32_t descriptor_number = 0;
    uint8_t descriptor_length = 0;
    uint32_t scte35_format_identifier = 0;
    uint8_t *p_descriptor_start = p;

    while(p - p_descriptor_start < program_info_length)
    {
        uint8_t descriptor_tag = *p;
        inc_ptr(p, 1);
        descriptor_length = *p;
        inc_ptr(p, 1);

        printf_xml(3, "<descriptor>\n");
        printf_xml(4, "<number>%d</number>\n", descriptor_number);
        printf_xml(4, "<tag>%d</tag>\n", descriptor_tag);
        printf_xml(4, "<length>%d</length>\n", descriptor_length);

        switch(descriptor_tag)
        {
            case VIDEO_STREAM_DESCRIPTOR:
            {
                /*
                    video_stream_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        multiple_frame_rate_flag 1 bslbf
                        frame_rate_code 4 uimsbf
                        MPEG_1_only_flag 1 bslbf
                        constrained_parameter_flag 1 bslbf
                        still_picture_flag 1 bslbf
                        if (MPEG_1_only_flag = = '0') {
                            profile_and_level_indication 8 uimsbf
                            chroma_format 2 uimsbf
                            frame_rate_extension_flag 1 bslbf
                            Reserved 5 bslbf
                        }
                    }
                */

                uint8_t multiple_frame_rate_flag = *p;
                inc_ptr(p, 1);
                uint8_t frame_rate_code = (multiple_frame_rate_flag & 0x78) >> 3;
                uint8_t mpeg_1_only_flag = (multiple_frame_rate_flag & 0x04) >> 2;
                uint8_t constrained_parameter_flag = (multiple_frame_rate_flag & 0x02) >> 1;
                uint8_t still_picture_flag = (multiple_frame_rate_flag & 0x01);
                multiple_frame_rate_flag >>= 7;

                printf_xml(4, "<type>video_stream_descriptor</type>\n");
                printf_xml(4, "<multiple_frame_rate_flag>%d</multiple_frame_rate_flag>\n", multiple_frame_rate_flag);
                printf_xml(4, "<frame_rate_code>0x%x</frame_rate_code>\n", frame_rate_code);
                printf_xml(4, "<mpeg_1_only_flag>%d</mpeg_1_only_flag>\n", mpeg_1_only_flag);
                printf_xml(4, "<constrained_parameter_flag>%d</constrained_parameter_flag>\n", constrained_parameter_flag);
                printf_xml(4, "<still_picture_flag>%d</still_picture_flag>\n", still_picture_flag);

                if (!mpeg_1_only_flag)
                {
                    uint8_t profile_and_level_indication = *p;
                    inc_ptr(p, 1);
                    uint8_t chroma_format = *p;
                    inc_ptr(p, 1);
                    uint8_t frame_rate_extension_flag = (chroma_format & 0x10) >> 4;
                    chroma_format >>= 6;
                    
                    printf_xml(4, "<profile_and_level_indication>0x%x</profile_and_level_indication>\n", profile_and_level_indication);
                    printf_xml(4, "<chroma_format>%d</chroma_format>\n", chroma_format);
                    printf_xml(4, "<frame_rate_extension_flag>%d</frame_rate_extension_flag>\n", frame_rate_extension_flag);
                }
            }
            break;
            case AUDIO_STREAM_DESCRIPTOR:
            {
                /*
                    audio_stream_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        free_format_flag 1 bslbf
                        ID 1 bslbf
                        layer 2 bslbf
                        variable_rate_audio_indicator 1 bslbf
                        reserved 3 bslbf
                    }
                */

                uint8_t free_format_flag = *p;
                inc_ptr(p, 1);
                uint8_t id = (free_format_flag & 0x40) >> 6;
                uint8_t layer = (free_format_flag & 0x30) >> 4;
                uint8_t variable_rate_audio_indicator = (free_format_flag & 0x08) >> 3;

                printf_xml(4, "<type>audio_stream_descriptor</type>\n");
                printf_xml(4, "<free_format_flag>%d</free_format_flag>\n", free_format_flag);
                printf_xml(4, "<id>%d</id>\n", id);
                printf_xml(4, "<layer>%d</layer>\n", layer);
                printf_xml(4, "<variable_rate_audio_indicator>%d</variable_rate_audio_indicator>\n", variable_rate_audio_indicator);
            }
            break;
            case HIERARCHY_DESCRIPTOR:
            {
                /*
                    hierarchy_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        reserved 4 bslbf
                        hierarchy_type 4 uimsbf
                        reserved 2 bslbf
                        hierarchy_layer_index 6 uimsbf
                        reserved 2 bslbf
                        hierarchy_embedded_layer_index 6 uimsbf
                        reserved 2 bslbf
                        hierarchy_channel 6 uimsbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>hierarchy_descriptor</type>\n");
            }
            break;
            case REGISTRATION_DESCRIPTOR:
            {
                /*
                    registration_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        format_identifier 32 uimsbf
                        for (i = 0; i < N; i++){
                            additional_identification_info 8 bslbf
                        }
                    }
                */

                uint32_t format_identifier = read_4_bytes(p);
                inc_ptr(p, 4);

                if(0x43554549 == format_identifier)
                    scte35_format_identifier = format_identifier; // Should be 0x43554549 (ASCII “CUEI”)

                inc_ptr(p, descriptor_length - 4);

                char sz_temp[5];
                char *pChar = (char *) &format_identifier;
                sz_temp[3] = *pChar++;
                sz_temp[2] = *pChar++;
                sz_temp[1] = *pChar++;
                sz_temp[0] = *pChar;
                sz_temp[4] = 0;
                printf_xml(4, "<type>registration_descriptor</type>\n");
                printf_xml(4, "<format_identifier>%s</format_identifier>\n", sz_temp);
                
                if (0 != scte35_format_identifier)
                    my_printf("        <scte35_format_identifier>0x%x</scte35_format_identifier>\n", scte35_format_identifier);
            }
            break;
            case DATA_STREAM_ALIGNMENT_DESCRIPTOR:
            {
                /*
                    data_stream_alignment_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        alignment_type 8 uimsbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>data_stream_alignment_descriptor</type>\n");
            }
            break;
            case TARGET_BACKGROUND_GRID_DESCRIPTOR:
            {
                /*
                    target_background_grid_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        horizontal_size 14 uimsbf
                        vertical_size 14 uimsbf
                        aspect_ratio_information 4 uimsbf
                    }            
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>target_background_grid_descriptor</type>\n");
            }
            break;
            case VIDEO_WINDOW_DESCRIPTOR:
            {
                /*
                    video_window_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        horizontal_offset 14 uimsbf
                        vertical_offset 14 uimsbf
                        window_priority 4 uimsbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>video_window_descriptor</type>\n");
            }
            break;
            case CA_DESCRIPTOR:
            {
                /*
                    CA_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        CA_system_ID 16 uimsbf
                        reserved 3 bslbf
                        CA_PID 13 uimsbf
                        for (i = 0; i < N; i++) {
                            private_data_byte 8 uimsbf
                        }
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>ca_descriptor</type>\n");
            }
            break;
            case ISO_639_LANGUAGE_DESCRIPTOR:
            {
                /*
                    ISO_639_language_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    for (i = 0; i < N; i++) {
                        ISO_639_language_code 24 bslbf
                        audio_type 8 bslbf
                    }
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>iso_639_language_descriptor</type>\n");
            }
            break;
            case SYSTEM_CLOCK_DESCRIPTOR:
            {
                /*
                    system_clock_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        external_clock_reference_indicator 1 bslbf
                        reserved 1 bslbf
                        clock_accuracy_integer 6 uimsbf
                        clock_accuracy_exponent 3 uimsbf
                        reserved 5 bslbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>system_clock_descriptor</type>\n");
            }
            break;
            case MULTIPLEX_BUFFER_UTILIZATION_DESCRIPTOR:
            {
                /*
                    Multiplex_buffer_utilization_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        bound_valid_flag 1 bslbf
                        LTW_offset_lower_bound 15 uimsbf
                        reserved 1 bslbf
                        LTW_offset_upper_bound 15 uimsbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>multiplex_buffer_utilization_descriptor</type>\n");
            }
            break;
            case COPYRIGHT_DESCRIPTOR:
            {
                /*
                    copyright_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        copyright_identifier 32 uimsbf
                        for (i = 0; i < N; i++){
                            additional_copyright_info 8 bslbf
                        }
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>copyright_descriptor</type>\n");
            }
            break;
            case MAXIMUM_BITRATE_DESCRIPTOR:
            {
                /*
                    maximum_bitrate_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        reserved 2 bslbf
                        maximum_bitrate 22 uimsbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>maximum_bitrate_descriptor</type>\n");
            }
            break;
            case PRIVATE_DATA_INDICATOR_DESCRIPTOR:
            {
                /*
                    private_data_indicator_descriptor() {
                        descriptor_tag 38 uimsbf
                        descriptor_length 38 uimsbf
                        private_data_indicator 32 uimsbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>private_data_indicator_descriptor</type>\n");
            }
            break;
            case SMOOTHING_BUFFER_DESCRIPTOR:
            {
                /*
                    smoothing_buffer_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        reserved 2 bslbf
                        sb_leak_rate 22 uimsbf
                        reserved 2 bslbf
                        sb_size 22 uimsbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>smoothing_buffer_descriptor</type>\n");
            }
            break;
            case STD_DESCRIPTOR:
            {
                /*
                    STD_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        reserved 7 bslbf
                        leak_valid_flag 1 bslbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>std_descriptor</type>\n");
            }
            case IBP_DESCRIPTOR:
            {
                /*
                    ibp_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        closed_gop_flag 1 uimsbf
                        identical_gop_flag 1 uimsbf
                        max_gop-length 14 uimsbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>ibp_descriptor</type>\n");
            }
            break;
            case MPEG_4_VIDEO_DESCRIPTOR:
            {
                /*
                    MPEG-4_video_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        MPEG-4_visual_profile_and_level 8 uimsbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>mpeg_4_video_descriptor</type>\n");
            }
            break;
            case MPEG_4_AUDIO_DESCRIPTOR:
            {
                /*
                    MPEG-4_audio_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        MPEG-4_audio_profile_and_level 8 uimsbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>mpeg_4_audio_descriptor</type>\n");
            }
            break;
            case IOD_DESCRIPTOR:
            {
                /*
                    IOD_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        Scope_of_IOD_label 8 uimsbf
                        IOD_label 8 uimsbf
                        InitialObjectDescriptor () 8 uimsbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>iod_descriptor</type>\n");
            }
            break;
            case SL_DESCRIPTOR:
            {
                /*
                    SL_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        ES_ID 16 uimsbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>sl_descriptor</type>\n");
            }
            break;
            case FMC_DESCRIPTOR:
            {
                /*
                    FMC_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        for (i = 0; i < descriptor_length; i + = 3) {
                            ES_ID 16 uimsbf
                            FlexMuxChannel 8 uimsbf
                        }
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>fmc_descriptor</type>\n");
            }
            break;
            case EXTERNAL_ES_ID_DESCRIPTOR:
            {
                /*
                    External_ES_ID_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        External_ES_ID 16 uimsbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>external_es_id_descriptor</type>\n");
            }
            break;
            case MUXCODE_DESCRIPTOR:
            {
                /*
                    Muxcode_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        for (i = 0; i < N; i++) {
                            MuxCodeTableEntry ()
                        }
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>muxcode_descriptor</type>\n");
            }
            break;
            case FMXBUFFERSIZE_DESCRIPTOR:
            {
                /*
                    FmxBufferSize_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        DefaultFlexMuxBufferDescriptor()
                        for (i=0; i<descriptor_length; i += 4) {
                            FlexMuxBufferDescriptor()
                        }
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>fmxbuffersize_descriptor</type>\n");
            }
            break;
            case MULTIPLEXBUFFER_DESCRIPTOR:
            {
                /*
                    MultiplexBuffer_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        MB_buffer_size 24 uimsbf
                        TB_leak_rate 24 uimsbf
                    }
                */
                inc_ptr(p, descriptor_length);
                printf_xml(4, "<type>multiplexbuffer_descriptor</type>\n");
            }
            break;
            default:
                inc_ptr(p, descriptor_length);
        }

        printf_xml(3, "</descriptor>\n");

        descriptor_number++;
    }

    return p - p_descriptor_start;
}

struct Frame
{
    int pid;
    int frameNumber;
    int totalPackets;
    pid_list_type pidList;

    Frame()
        : pid(-1)
        , frameNumber(0)
        , totalPackets(0)
    {}
};

enum eStreamID
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

uint32_t read_time_stamp(uint8_t *&p)
{
    uint32_t byte = *p;
    inc_ptr(p, 1);

    uint32_t time_stamp = (byte & 0x0E) << 28;

    uint32_t two_bytes = read_2_bytes(p);
    inc_ptr(p, 2);

    time_stamp |= (two_bytes & 0xFFFE) << 13;

    two_bytes = read_2_bytes(p);
    inc_ptr(p, 2);

    time_stamp |= (two_bytes & 0xFFFE) >> 1;

    return time_stamp;
}

size_t process_PES_packet_header(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint8_t byte = *p;
    inc_ptr(p, 1);

    uint8_t PES_scrambling_control = (byte & 0x30) >> 4;
    uint8_t PES_priority = (byte & 0x08) >> 3;
    uint8_t data_alignment_indicator = (byte & 0x04) >> 2;
    uint8_t copyright = (byte & 0x02) >> 1;
    uint8_t original_or_copy = byte & 0x01;

    byte = *p;
    inc_ptr(p, 1);

    uint8_t PTS_DTS_flags = (byte & 0xC0) >> 6;
    uint8_t ESCR_flag = (byte & 0x20) >> 5;
    uint8_t ES_rate_flag = (byte & 0x10) >> 4;
    uint8_t DSM_trick_mode_flag = (byte & 0x08) >> 3;
    uint8_t additional_copy_info_flag = (byte & 0x04) >> 2;
    uint8_t PES_CRC_flag = (byte & 0x02) >> 1;
    uint8_t PES_extension_flag = byte & 0x01;

    /*
        PES_header_data_length – An 8-bit field specifying the total number of bytes occupied by the optional fields and any
        stuffing bytes contained in this PES packet header. The presence of optional fields is indicated in the byte that precedes
        the PES_header_data_length field.
    */
    uint8_t PES_header_data_length = *p;
    inc_ptr(p, 1);

    if(2 == PTS_DTS_flags)
    {
        uint32_t PTS = read_time_stamp(p);
    }

    if(3 == PTS_DTS_flags)
    {
        uint32_t PTS = read_time_stamp(p);
        uint32_t DTS = read_time_stamp(p);
    }

    if(ESCR_flag) // 6 bytes
    {
        uint32_t byte = *p;
        inc_ptr(p, 1);

        // 31, 31, 30
        uint32_t ESCR = (byte & 0x38) << 27;

        // 29, 28
        ESCR |= (byte & 0x03) << 29;

        byte = *p;
        inc_ptr(p, 1);

        // 27, 26, 25, 24, 23, 22, 21, 20
        ESCR |= byte << 19;

        byte = *p;
        inc_ptr(p, 1);

        // 19, 18, 17, 16, 15
        ESCR |= (byte & 0xF8) << 11;

        // 14, 13
        ESCR |= (byte & 0x03) << 13;

        byte = *p;
        inc_ptr(p, 1);

        // 12, 11, 10, 9, 8, 7, 6, 5
        ESCR |= byte << 4;

        byte = *p;
        inc_ptr(p, 1);

        // 4, 3, 2, 1, 0
        ESCR |= (byte & 0xF8) >> 3;

        uint32_t ESCR_ext = (byte & 0x03) << 7;

        byte = *p;
        inc_ptr(p, 1);

        ESCR_ext |= (byte & 0xFE) >> 1;
    }

    if(ES_rate_flag)
    {
        uint32_t four_bytes = *p;
        inc_ptr(p, 1);
        four_bytes <<= 8;

        four_bytes |= *p;
        inc_ptr(p, 1);
        four_bytes <<= 8;

        four_bytes |= *p;
        inc_ptr(p, 1);
        four_bytes <<= 8;

        uint32_t ES_rate = (four_bytes & 0x7FFFFE) >> 1;
    }

    if(DSM_trick_mode_flag)
    {
        // Table 2-24 – Trick mode control values
        // Value Description
        // '000' Fast forward
        // '001' Slow motion
        // '010' Freeze frame
        // '011' Fast reverse
        // '100' Slow reverse
        // '101'-'111' Reserved

        uint8_t byte = *p;
        inc_ptr(p, 1);

        uint8_t trick_mode_control = byte >> 5;

        if(0 == trick_mode_control) // Fast forward
        {
            uint8_t field_id = (byte & 0x18) >> 3;
            uint8_t intra_slice_refresh = (byte & 0x04) >> 2;
            uint8_t frequency_truncation = byte & 0x03;
        }
        else if(1 == trick_mode_control) // Slow motion
        {
            uint8_t rep_cntrl = byte & 0x1f;
        }
        else if(2 == trick_mode_control) // Freeze frame
        {
            uint8_t field_id = (byte & 0x18) >> 3;
        }
        else if(3 == trick_mode_control) // Fast reverse
        {
            uint8_t field_id = (byte & 0x18) >> 3;
            uint8_t intra_slice_refresh = (byte & 0x04) >> 2;
            uint8_t frequency_truncation = byte & 0x03;
        }
        else if(4 == trick_mode_control) // Slow reverse
        {
            uint8_t rep_cntrl = byte & 0x1f;
        }
    }

    if(additional_copy_info_flag)
    {
        uint8_t byte = *p;
        inc_ptr(p, 1);

        uint8_t additional_copy_info = byte & 0x7F;
    }

    if(PES_CRC_flag)
    {
        uint16_t previous_PES_packet_CRC = read_2_bytes(p);
        inc_ptr(p, 2);
    }

    if(PES_extension_flag)
    {
        uint8_t byte = *p;
        inc_ptr(p, 1);

        uint8_t PES_private_data_flag = (byte & 0x80) >> 7;
        uint8_t pack_header_field_flag = (byte & 0x40) >> 6;
        uint8_t program_packet_sequence_counter_flag = (byte & 0x20) >> 5;
        uint8_t P_STD_buffer_flag = (byte & 0x10) >> 4;
        // 3 bits Reserved
        uint8_t PES_extension_flag_2 = byte & 0x01;

        if(PES_private_data_flag)
        {
            uint8_t PES_private_data[16];
            memcpy(PES_private_data, p, 16);
            inc_ptr(p, 16);
        }

        if(pack_header_field_flag)
        {
            uint8_t pack_field_length = *p;
            inc_ptr(p, 1);

            // pack_header is here
            // http://stnsoft.com/DVD/packhdr.html

            inc_ptr(p, pack_field_length);
        }

        if(program_packet_sequence_counter_flag)
        {
            uint8_t byte = *p;
            inc_ptr(p, 1);

            uint8_t program_packet_sequence_counter = byte & 0x07F;

            byte = *p;
            inc_ptr(p, 1);

            uint8_t MPEG1_MPEG2_identifier = (byte & 0x40) >> 6;
        }

        if(P_STD_buffer_flag)
        {
            uint16_t two_bytes = read_2_bytes(p);
            inc_ptr(p, 2);

            uint8_t P_STD_buffer_scale = (two_bytes & 0x2000) >> 13;
            uint8_t P_STD_buffer_size = two_bytes & 0x1FFF;
        }

        if(PES_extension_flag_2)
        {
            uint8_t byte = *p;
            inc_ptr(p, 1);

            uint8_t PES_extension_field_length = byte & 0x7F;

            byte = *p;
            inc_ptr(p, 1);

            uint8_t stream_id_extension_flag = (byte & 0x80) >> 7;

            if(0 == stream_id_extension_flag)
            {
                uint8_t stream_id_extension = byte & 0x7F;

                // Reserved

                inc_ptr(p, PES_extension_field_length);
            }
        }
    }

    return p - pStart;
}

enum eMpeg2StartCode
{
    picture_start_code = 0,
    slice_start_codes_begin = 1,
    slice_start_codes_end = 0xAF,
    user_data_start_code = 0xB2,
    sequence_header_code = 0xB3,
    sequence_error_code = 0xB4,
    extension_start_code = 0xB5,
    sequence_end_code = 0xB7,
    group_start_code = 0xB8,
    system_start_codes_begin = 0xB9,
    system_start_codes_end = 0xFF
};

// MPEG2 spec, 13818-2, 6.2.2.1
size_t process_mpeg2_sequence_header(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint32_t four_bytes = read_4_bytes(p);
    inc_ptr(p, 4);

    uint32_t horizontal_size_value = (four_bytes & 0xFFF00000) >> 20;
    uint32_t vertical_size_value = (four_bytes & 0x000FFF00) >> 8;
    uint8_t aspect_ratio_information = (four_bytes & 0xF0) >> 4;
    uint8_t frame_rate_code = four_bytes & 0x0F;

    four_bytes = read_4_bytes(p);
    inc_ptr(p, 4);

    // At this point p is one bit in to the intra_quantizer_matrix

    uint32_t bit_rate_value = (four_bytes & 0xFFFFC000) >> 14;
    uint16_t vbv_buffer_size_value = (four_bytes & 0x1FF8) >> 3;
    uint8_t constrained_parameters_flag = (four_bytes & 0x4) >> 2;
    uint8_t load_intra_quantizer_matrix = (four_bytes & 0x2) >> 1;

    uint8_t load_non_intra_quantizer_matrix = 0;

    if(load_intra_quantizer_matrix)
    {
        inc_ptr(p, 63);
        load_non_intra_quantizer_matrix = *p;
        inc_ptr(p, 1);
        load_non_intra_quantizer_matrix &= 0x1;
    }
    else
        load_non_intra_quantizer_matrix = four_bytes & 0x1;

    if(load_non_intra_quantizer_matrix)
        inc_ptr(p, 64);

    g_current_mpeg2_extension_type = sequence_extension;

    return p - pStart;
}

/*
enum eExtensionType
{
    sequence_extension = 0,
    extension_and_user_data_0,
    extension_and_user_data_1,
    extension_and_user_data_2,
    unknown_extension
};
*/

// MPEG2 spec, 13818-2, 6.2.2.2.1
size_t process_mpeg2_extension(uint8_t *&p)
{
    uint8_t *pStart = p;

    switch(g_current_mpeg2_extension_type)
    {
        case sequence_extension:
        break;
        
        case extension_and_user_data_0:
        break;

        case extension_and_user_data_1:
        break;

        case extension_and_user_data_2:
        break;
    }

    return p - pStart;
}

size_t process_mpeg2_video_PES(uint8_t *&p, size_t PES_packet_data_length)
{
    uint8_t *pStart = p;
    size_t bytes_processed = 0;

    while(bytes_processed < PES_packet_data_length)
    {
        uint32_t start_code = read_4_bytes(p);
        inc_ptr(p, 4);

        uint32_t start_code_prefix = (start_code & 0xFFFFFF00) >> 8;

        assert(0x000001 == start_code_prefix);

        start_code &= 0x000000FF;
        switch(start_code)
        {
            case picture_start_code:
            break;

            case user_data_start_code:
            break;

            case sequence_header_code:
                bytes_processed += process_mpeg2_sequence_header(p);
            break;

            case sequence_error_code:
            break;

            case extension_start_code:
                bytes_processed += process_mpeg2_extension(p);
            break;

            case sequence_end_code:
            break;

            case group_start_code:
            break;

            default:
            {
                if(start_code >= slice_start_codes_begin &&
                   start_code <= slice_start_codes_end)
                {
                }
                else if(start_code >= system_start_codes_begin &&
                        start_code <= system_start_codes_end)
                {
                }
            }
        }
    }

    return p - pStart;
}

size_t process_PES_packet(uint8_t *&p, int64_t packet_start, eStreamType stream_type, bool payload_unit_start)
{
    if(false == payload_unit_start)
    {
        size_t PES_packet_length = g_packet_size - (g_ptr_position - packet_start);
        inc_ptr(p, PES_packet_length);
        return PES_packet_length;
    }

    uint32_t four_bytes = read_4_bytes(p);
    inc_ptr(p, 4);

    uint32_t packet_start_code_prefix = (four_bytes & 0xffffff00) >> 8;
    uint8_t stream_id = four_bytes & 0xff;

    /* 2.4.3.7
      PES_packet_length – A 16-bit field specifying the number of bytes in the PES packet following the last byte of the field.
      A value of 0 indicates that the PES packet length is neither specified nor bounded and is allowed only in
      PES packets whose payload consists of bytes from a video elementary stream contained in Transport Stream packets.
    */

    int64_t PES_packet_length = read_2_bytes(p);
    inc_ptr(p, 2);

    if(0 == PES_packet_length)
        PES_packet_length = g_packet_size - (g_ptr_position - packet_start);

    if (stream_id != program_stream_map &&
        stream_id != padding_stream &&
        stream_id != private_stream_2 &&
        stream_id != ECM_stream &&
        stream_id != EMM_stream &&
        stream_id != program_stream_directory &&
        stream_id != DSMCC_stream &&
        stream_id != itu_h222_e_stream)
    {
        size_t header_size = process_PES_packet_header(p);

        /*
            stuffing_byte – This is a fixed 8-bit value equal to '1111 1111' that can be inserted by the encoder, for example to meet
            the requirements of the channel. It is discarded by the decoder. No more than 32 stuffing bytes shall be present in one
            PES packet header.
        */

        while(*p == 0xFF)
        {
            p++;
            inc_ptr(p,1);
        }

        /*
            PES_packet_data_byte – PES_packet_data_bytes shall be contiguous bytes of data from the elementary stream
            indicated by the packet's stream_id or PID. When the elementary stream data conforms to ITU-T
            Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 13818-3, the PES_packet_data_bytes shall be byte aligned to the bytes of
            this Recommendation | International Standard. The byte-order of the elementary stream shall be preserved. The number
            of PES_packet_data_bytes, N, is specified by the PES_packet_length field. N shall be equal to the value indicated in the
            PES_packet_length minus the number of bytes between the last byte of the PES_packet_length field and the first
            PES_packet_data_byte.

            In the case of a private_stream_1, private_stream_2, ECM_stream, or EMM_stream, the contents of the
            PES_packet_data_byte field are user definable and will not be specified by ITU-T | ISO/IEC in the future.
        */

        // PES_packet_data here
        size_t PES_packet_data_length = PES_packet_length - header_size;

        if(eMPEG2_Video == stream_type)
        {
            process_mpeg2_video_PES(p, PES_packet_data_length);

            inc_ptr(p, PES_packet_data_length);
        }
        else
        {
            inc_ptr(p, PES_packet_data_length);
        }
    }
    else if (stream_id == program_stream_map ||
             stream_id == private_stream_2 ||
             stream_id == ECM_stream ||
             stream_id == EMM_stream ||
             stream_id == program_stream_directory ||
             stream_id == DSMCC_stream ||
             stream_id == itu_h222_e_stream)
    {
        // PES_packet_data here
        inc_ptr(p, PES_packet_length);
    }
    else if (stream_id == padding_stream)
    {
        // Padding bytes here
        inc_ptr(p, PES_packet_length);
    }

    return PES_packet_length;
}

// Process each PID for each 188 byte packet
int16_t process_pid(uint16_t pid, uint8_t *&p, int64_t packet_start, size_t packet_num, bool payload_unit_start)
{
    if(pid == 0x00)
    {
        static bool g_b_want_pat = true;

        if(g_b_want_pat)
        {
            if(g_b_terse)
            {
                printf_xml(1, "<packet start=\"%llu\">\n", packet_start);
                printf_xml(2, "<number>%zd</number>\n", packet_num);
                printf_xml(2, "<pid>0x%x</pid>\n", pid);
                printf_xml(2, "<payload_unit_start_indicator>0x%x</payload_unit_start_indicator>\n", payload_unit_start ? 1 : 0);
            }

            read_pat(p, payload_unit_start);

            if(g_b_terse)
                printf_xml(1, "</packet>\n");
        }

        if(g_b_terse)
            g_b_want_pat = false;
    }
    else if(pid == g_program_map_pid)
    {
        static bool g_b_want_pmt = true;

        if(g_b_want_pmt)
        {
            if(g_b_terse)
            {
                printf_xml(1, "<packet start=\"%llu\">\n", packet_start);
                printf_xml(2, "<number>%zd</number>\n", packet_num);
                printf_xml(2, "<pid>0x%x</pid>\n", pid);
                printf_xml(2, "<payload_unit_start_indicator>0x%x</payload_unit_start_indicator>\n", payload_unit_start ? 1 : 0);
            }

            read_pmt(p, payload_unit_start);

            if(g_b_terse)
                printf_xml(1, "</packet>\n");
        }

        if(g_b_terse)
            g_b_want_pmt = false;
    }
    else
    {
        if(false == g_b_terse)
        {
            // Here, p is pointing at actual data, like video or audio.
            // For now just print the data's type.
            printf_xml(2, "<type_name>%s</type_name>\n", g_pid_map[pid]);
        }
        //else

        process_PES_packet(p, packet_start, g_pid_to_type_map[pid], payload_unit_start);

        {
            static Frame videoFrame;
            static Frame audioFrame;
            static size_t lastPid = -1;

            Frame *p_frame = nullptr;

            switch(g_pid_to_type_map[pid])
            {
                case eMPEG1_Video:
                case eMPEG2_Video:
                case eMPEG4_Video:
                case eH264_Video:
                case eDigiCipher_II_Video:
                case eMSCODEC_Video:
                    p_frame = &videoFrame;
                break;

                case eMPEG1_Audio:
                case eMPEG2_Audio:
                case eMPEG2_AAC_Audio:
                case eMPEG4_LATM_AAC_Audio:
                case eA52_AC3_Audio:
                case eHDMV_DTS_Audio:
                case eA52b_AC3_Audio:
                case eSDDS_Audio:
                    p_frame = &audioFrame;
                break;
            }

            if(p_frame)
            {
                bool bNewSet = false;

                if(payload_unit_start)
                {
                    if(p_frame->pidList.size())
                    {
                        for(pid_list_type::iterator p = p_frame->pidList.begin(); p != p_frame->pidList.end(); p++)
                            p_frame->totalPackets += p->num_packets;

                        printf_xml(1,
                                   "<frame number=\"%d\" name=\"%s\" packets=\"%d\" pid=\"0x%x\">\n",
                                   p_frame->frameNumber++, p_frame->pidList[0].pid_name.c_str(), p_frame->totalPackets, pid);

                        for(pid_list_type::iterator p = p_frame->pidList.begin(); p != p_frame->pidList.end(); p++)
                            printf_xml(2, "<slice byte=\"%llu\" packets=\"%d\"/>\n", p->pid_byte_location, p->num_packets);

                        printf_xml(1, "</frame>\n");

                        p_frame->totalPackets = 0;
                    }

                    p_frame->pidList.clear();
                    bNewSet = true;
                }

                if(-1 != lastPid && pid != lastPid)
                    bNewSet = true;

                if(bNewSet)
                {
                    pid_entry_type p(g_pid_map[pid], 1, packet_start);
                    p_frame->pidList.push_back(p);
                }
                else
                {
                    pid_entry_type &p = p_frame->pidList.back();
                    p.num_packets++;
                }
            }

            lastPid = pid;
        }
    }

    return 0;
}

uint8_t process_adaptation_field(unsigned int indent, uint8_t *&p)
{
    uint8_t adaptation_field_length = *p;
    inc_ptr(p, 1);

    uint8_t *pAdapatationFieldStart = p;

    if(adaptation_field_length)
    {
        uint8_t byte = *p;
        inc_ptr(p, 1);

        uint8_t discontinuity_indicator = (byte & 0x80) >> 7;
        uint8_t random_access_indicator = (byte & 0x40) >> 6;
        uint8_t elementary_stream_priority_indicato = (byte & 0x20) >> 5;
        uint8_t PCR_flag = (byte & 0x10) >> 4;
        uint8_t OPCR_flag = (byte & 0x08) >> 3;
        uint8_t splicing_point_flag = (byte & 0x04) >> 2;
        uint8_t transport_private_data_flag = (byte & 0x02) >> 1;
        uint8_t adaptation_field_extension_flag = (byte & 0x02) >> 1;

        if(PCR_flag)
        {
            uint32_t four_bytes = read_4_bytes(p);
            inc_ptr(p, 4);

            uint16_t two_bytes = read_2_bytes(p);
            inc_ptr(p, 2);

            uint64_t program_clock_reference_base = four_bytes;
            program_clock_reference_base <<= 1;
            program_clock_reference_base |= (two_bytes & 0x80) >> 7;

            uint16_t program_clock_reference_extension = two_bytes & 0x1ff;
        }

        if(OPCR_flag)
        {
            uint32_t four_bytes = read_4_bytes(p);
            inc_ptr(p, 4);

            uint16_t two_bytes = read_2_bytes(p);
            inc_ptr(p, 2);

            uint64_t original_program_clock_reference_base = four_bytes;
            original_program_clock_reference_base <<= 1;
            original_program_clock_reference_base |= (two_bytes & 0x80) >> 7;

            uint16_t original_program_clock_reference_extension = two_bytes & 0x1ff;
        }

        if(splicing_point_flag)
        {
            uint8_t splice_countdown = *p;
            inc_ptr(p, 1);
        }

        if(transport_private_data_flag)
        {
            uint8_t transport_private_data_length = *p;
            inc_ptr(p, 1);

            for(unsigned int i = 0; i < transport_private_data_length; i++)
                p++;
        }

        if(adaptation_field_extension_flag)
        {
            size_t adaptation_field_extension_length = *p;
            inc_ptr(p, 1);

            uint8_t *pAdapatationFieldExtensionStart = p;

            uint8_t byte = *p;
            inc_ptr(p, 1);

            uint8_t ltw_flag = (byte & 0x80) >> 7;
            uint8_t piecewise_rate_flag = (byte & 0x40) >> 6;
            uint8_t seamless_splice_flag = (byte & 0x20) >> 5;

            if(ltw_flag)
            {
                uint16_t two_bytes = read_2_bytes(p);
                inc_ptr(p, 2);

                uint8_t ltw_valid_flag = (two_bytes & 0x8000) >> 15;
                uint16_t ltw_offset = two_bytes & 0x7fff;
            }

            if(piecewise_rate_flag)
            {
                uint16_t two_bytes = read_2_bytes(p);
                inc_ptr(p, 2);

                uint32_t piecewise_rate = two_bytes & 0x3fffff;
            }

            if(seamless_splice_flag)
            {
                uint32_t byte = *p;
                inc_ptr(p, 1);

                uint32_t DTS_next_AU;
                uint8_t splice_type = (byte & 0xf0) >> 4;
                DTS_next_AU = (byte & 0xe) << 28;

                uint32_t two_bytes = read_2_bytes(p);
                inc_ptr(p, 2);

                DTS_next_AU |= (two_bytes & 0xfe) << 13;

                two_bytes = read_2_bytes(p);
                inc_ptr(p, 2);

                DTS_next_AU |= (two_bytes & 0xfe) >> 1;
            }

            size_t N = adaptation_field_extension_length - (p - pAdapatationFieldExtensionStart);
            for(unsigned int i = 0; i < N; i++)
                p++; // reserved
        }

        size_t N = adaptation_field_length - (p - pAdapatationFieldStart);
        for(unsigned int i = 0; i < N; i++)
            p++; // stuffing_byte
    }

    return adaptation_field_length;
}

// Get the PID and other info
int16_t process_packet(uint8_t *packet, size_t packetNum)
{
    uint8_t *p = NULL;
    int16_t ret = -1;
    int64_t packet_start = g_ptr_position;

    if(false == g_b_terse)
    {
        printf_xml(1, "<packet start=\"%llu\">\n", g_ptr_position);
        printf_xml(2, "<number>%zd</number>\n", packetNum);
    }

    p = packet;

    if (0x47 != *p)
    {
        printf_xml(2, "<error>Packet %zd does not start with 0x47</error>\n", packetNum);
        fprintf(stderr, "Error: Packet %zd does not start with 0x47\n", packetNum);
        goto process_packet_error;
    }

    // Skip the sync byte 0x47
    inc_ptr(p, 1);

    uint16_t PID = read_2_bytes(p);
    inc_ptr(p, 2);

    uint8_t transport_error_indicator = (PID & 0x8000) >> 15;
    uint8_t payload_unit_start_indicator = (PID & 0x4000) >> 14;

    uint8_t transport_priority = (PID & 0x2000) >> 13;

    PID &= 0x1FFF;

    uint8_t adaptation_field_control = 1;

    if(false == g_b_terse)
    {
        printf_xml(2, "<pid>0x%x</pid>\n", PID);
        printf_xml(2, "<payload_unit_start_indicator>0x%x</payload_unit_start_indicator>\n", payload_unit_start_indicator);

        // Move beyond the 32 bit header
        uint8_t final_byte = *p;
        inc_ptr(p, 1);

        uint8_t transport_scrambling_control = (final_byte & 0xC0) >> 6;
        adaptation_field_control = (final_byte & 0x30) >> 4;
        uint8_t continuity_counter = (final_byte & 0x0F) >> 4;

        printf_xml(2, "<transport_error_indicator>0x%x</transport_error_indicator>\n", transport_error_indicator);
        printf_xml(2, "<transport_priority>0x%x</transport_priority>\n", transport_priority);
        printf_xml(2, "<transport_scrambling_control>0x%x</transport_scrambling_control>\n", transport_scrambling_control);
        printf_xml(2, "<adaptation_field_control>0x%x</adaptation_field_control>\n", adaptation_field_control);
        printf_xml(2, "<continuity_counter>0x%x</continuity_counter>\n", continuity_counter);
    }
    else
        inc_ptr(p, 1);

    /*
        Table 2-5 – Adaptation field control values
            Value  Description
             00    Reserved for future use by ISO/IEC
             01    No adaptation_field, payload only
             10    Adaptation_field only, no payload
             11    Adaptation_field followed by payload
    */
    uint8_t adaptation_field_length = 0;

    if(2 == adaptation_field_control ||
       3 == adaptation_field_control)
    {
        adaptation_field_length = process_adaptation_field(2, p);
    }

    uint8_t data_length = 184 - adaptation_field_length;

    if(2 != adaptation_field_control)
        ret = process_pid(PID, p, packet_start, packetNum, 1 == payload_unit_start_indicator);
    else
        ret = adaptation_field_length;

process_packet_error:

    if(false == g_b_terse)
        printf_xml(1, "</packet>\n");

    return ret;
}

// It all starts here
int main(int argc, char* argv[])
{
    tinyxml2::XMLDocument* doc = new tinyxml2::XMLDocument();

    if (0 == argc)
    {
        fprintf(stderr, "%s: Output extensive xml representation of MP2TS file to stdout\n", argv[0]);
        fprintf(stderr, "Usage: %s [-g] [-p] [-q] mp2ts_file\n", argv[0]);
        fprintf(stderr, "-g: Generate an OpenGL GUI representing the MP2TS\n");
        fprintf(stderr, "-p: Print progress on a single line to stderr\n");
        fprintf(stderr, "-q: No output. Run through the file and only print errors\n");
        fprintf(stderr, "-v: Verbose output. Careful with this one\n");
        return 0;
    }

    for (int i = 1; i < argc - 1; i++)
    {
        if (0 == strcmp("-d", argv[i]))
            g_b_debug = true;

        if (0 == strcmp("-g", argv[i]))
            g_b_gui = true;

        if (0 == strcmp("-p", argv[i]))
            g_b_progress = true;

        if(0 == strcmp("-q", argv[i]))
            g_b_xml = false;

        if(0 == strcmp("-v", argv[i]))
            g_b_terse = false;
    }

    uint8_t *packet_buffer, *packet;
	uint16_t program_map_pid = 0;
    unsigned int packet_num = 0;

	size_t packet_buffer_size = 0;
    int64_t total_read = 0;
    int64_t read_block_size = 0;

	FILE *f = nullptr;
	fopen_s(&f, argv[argc - 1], "rb");

	if (nullptr == f)
    {
        fprintf(stderr, "%s: Can't open input file", argv[0]);
		return -1;
    }

    // Determine the size of the file
    int64_t file_size = 0;

#ifdef WINDOWS
    struct __stat64 stat64_buf;
    _stat64(argv[argc - 1], &stat64_buf);
    file_size = stat64_buf.st_size;
#else
    fseek(f, 0L, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0L, SEEK_SET);
#endif

    // Need to determine packet size.
    // Standard is 188, but digital video cameras add a 4 byte timecode
    // before the 188 byte packet, making the packet size 192.
    // https://en.wikipedia.org/wiki/MPEG_transport_stream

    uint8_t temp_buffer[5];
    fread(temp_buffer, 1, 5, f);

    if(0x47 == temp_buffer[0])
        g_packet_size = 188;
    else if(0x47 == temp_buffer[4])
        g_packet_size = 192;
    else
    {
        fprintf(stderr, "%s: Can't recognize the input file", argv[0]);
        return -1;
    }

    // Go back to the beginning of the file
    fseek(f, 0L, SEEK_SET);

    if(file_size > 10000*g_packet_size)
        read_block_size = 10000*g_packet_size;
    else
        read_block_size = file_size;

    packet_buffer = new uint8_t[read_block_size];

    // Fill in the type-to-text maps
    init_stream_types();

    // This has to be done by hand
    g_pid_map[0x1FFF] = "NULL Packet";

    // Read each 188 byte packet and process the packet
	packet_buffer_size = fread(packet_buffer, 1, read_block_size, f);
    packet = packet_buffer;

//    printf_xml(doc, "file", "");
//    printf_xml(doc, "name", "foo");
//    printf_xml(doc, "/file", "");

    printf_xml(0, "<?xml version = \"1.0\" encoding = \"UTF-8\"?>\n");
    printf_xml(0, "<file>\n");
    printf_xml(1, "<name>%s</name>\n", argv[argc - 1]);
    printf_xml(1, "<file_size>%llu</file_size>\n", file_size);
    printf_xml(1, "<packet_size>%d</packet_size>\n", g_packet_size);
    if(g_b_terse)
        printf_xml(1, "<terse>1</terse>\n");
    else
        printf_xml(1, "<terse>0</terse>\n");

    float step = 1.f;
    float nextStep = 0.f;
    float progress = 0.f;

	while((size_t) (packet - packet_buffer) < packet_buffer_size)
	{
        int err = 0;

        if(192 == g_packet_size)
            err = process_packet(packet + 4, packet_num);
        else
            err = process_packet(packet, packet_num);

        if(0 != err)
            goto error;

        total_read += g_packet_size;
        g_ptr_position = total_read;

        if(g_b_progress)
        {
            if(progress >= nextStep)
            {
                fprintf(stderr, "Total bytes processed: %llu, %2.2f%%\r", total_read, progress);
                nextStep += step;
            }

            progress = ((float)total_read / (float)file_size) * 100.f;
        }

        if(0 == (total_read % read_block_size))
        {
            packet_buffer_size = fread(packet_buffer, 1, read_block_size, f);
            packet = packet_buffer;
        }
        else
            packet += g_packet_size;

        assert(packet_buffer_size > 0);

        packet_num++;
    }

error:
    printf_xml(0, "</file>\n");

    delete packet_buffer;

	fclose(f);

	return 0;
}