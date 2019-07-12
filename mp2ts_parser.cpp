// mp2ts_parser.cpp : Defines the entry point for the console application.
//
// (C) 2019 Mike Cancilla
//
// Feel free to use this code.  An acknowledgement would be nice.
//

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
#include <cassert>
#include <cstdarg>
#include <vector>
#include <map>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw_gl3.h"

#define READ_2_BYTES(p) *p | (*(p+1) << 8); p+=2;

// Forward function definitions
size_t read_descriptors(uint8_t *p, uint16_t program_info_length);

// Type definitions
struct pid_entry_type
{
    std::string pid_name;
    size_t pid_byte_location;

    pid_entry_type(std::string pid_name, size_t pid_byte_location)
        : pid_name(pid_name)
        , pid_byte_location(pid_byte_location)
    {
    }
};

typedef std::map <uint16_t, char *> stream_map_type; // ID, Name
typedef std::map <uint16_t, char *> pid_map_type; // ID, Name

typedef std::vector<pid_entry_type> pid_list_type;

// Global definitions
stream_map_type g_stream_map;
pid_map_type g_pid_map;
pid_list_type g_pid_list;

bool g_b_xml = false;
bool g_b_progress = false;
bool g_b_gui = false;

int16_t g_program_number = -1;
int16_t g_program_map_pid = -1;
int16_t g_scte32_pid = -1;
size_t g_ptr_position = 0;

GLFWwindow* g_window = NULL;

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
    if(g_b_xml)
    {
        va_list arg_list;
        va_start(arg_list, format);
        vprintf(format, arg_list);
        va_end(arg_list);
    }
}

void inline inc_ptr(uint8_t *&p, int bytes)
{
    p += bytes;
    g_ptr_position += bytes;
}

// Initialize a map of ID to string type
void init_stream_types()
{
    g_stream_map[0x0] = "Reserved";	                            
    g_stream_map[0x1] = "MPEG - 1 Video";
    g_stream_map[0x2] = "MPEG - 2 Video";
    g_stream_map[0x3] = "MPEG - 1 Audio";
    g_stream_map[0x4] = "MPEG - 2 Audio";
    g_stream_map[0x5] = "ISO 13818 - 1 private sections";
    g_stream_map[0x6] = "ISO 13818 - 1 PES private data";
    g_stream_map[0x7] = "ISO 13522 MHEG";
    g_stream_map[0x8] = "ISO 13818 - 1 DSM - CC";
    g_stream_map[0x9] = "ISO 13818 - 1 auxiliary";
    g_stream_map[0xa] = "ISO 13818 - 6 multi - protocol encap";
    g_stream_map[0xb] = "ISO 13818 - 6 DSM - CC U - N msgs";
    g_stream_map[0xc] = "ISO 13818 - 6 stream descriptors";
    g_stream_map[0xd] = "ISO 13818 - 6 sections";
    g_stream_map[0xe] = "ISO 13818 - 1 auxiliary";
    g_stream_map[0xf] = "MPEG - 2 AAC Audio";
    g_stream_map[0x10] = "MPEG - 4 Video";
    g_stream_map[0x11] = "MPEG - 4 LATM AAC Audio";
    g_stream_map[0x12] = "MPEG - 4 generic";
    g_stream_map[0x13] = "ISO 14496 - 1 SL - packetized";
    g_stream_map[0x14] = "ISO 13818 - 6 Synchronized Download Protocol";
    g_stream_map[0x1b] = "H.264 Video";
    g_stream_map[0x80] = "DigiCipher II Video";
    g_stream_map[0x81] = "A52 / AC - 3 Audio";
    g_stream_map[0x82] = "HDMV DTS Audio";
    g_stream_map[0x83] = "LPCM Audio";
    g_stream_map[0x84] = "SDDS Audio";
    g_stream_map[0x85] = "ATSC Program ID";
    g_stream_map[0x86] = "DTS - HD Audio";
    g_stream_map[0x87] = "E - AC - 3 Audio";
    g_stream_map[0x8a] = "DTS Audio";
    g_stream_map[0x91] = "A52b / AC - 3 Audio";
    g_stream_map[0x92] = "DVD_SPU vls Subtitle";
    g_stream_map[0x94] = "SDDS Audio";
    g_stream_map[0xa0] = "MSCODEC Video";
    g_stream_map[0xea] = "Private ES(VC - 1)";
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
int16_t read_pat(uint8_t *p)
{
    uint8_t pointer_field = *p; // Spec 2.4.4.1
    inc_ptr(p, 1);
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

    my_printf("    <program_association_table>\n");
    my_printf("      <pointer_field>0x%x</pointer_field>\n", pointer_field);
    my_printf("      <table_id>0x%x</table_id>\n", table_id);
    my_printf("      <section_syntax_indicator>%d</section_syntax_indicator>\n", section_syntax_indicator);
    my_printf("      <section_length>%d</section_length>\n", section_length);
    my_printf("      <transport_stream_id>0x%x</transport_stream_id>\n", transport_stream_id);
    my_printf("      <version_number>0x%x</version_number>\n", version_number);
    my_printf("      <current_next_indicator>0x%x</current_next_indicator>\n", current_next_indicator);
    my_printf("      <section_number>0x%x</section_number>\n", section_number);
    my_printf("      <last_section_number>0x%x</last_section_number>\n", last_section_number);

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
        }
        else
        {
            g_program_map_pid = read_2_bytes(p);
            inc_ptr(p, 2);
            g_program_map_pid &= 0x1FFF;
        }

        my_printf("      <program>\n");
        my_printf("        <number>%d</number>\n", g_program_number);

        if(network_pid)
            my_printf("        <network_pid>0x%x</network_pid>\n", network_pid);
        else
            my_printf("        <program_map_pid>0x%x</program_map_pid>\n", g_program_map_pid);

        my_printf("      </program>\n");
    }

    my_printf("    </program_association_table>\n");

    return 0;
}

// 2.4.4.8 Program Map Table
//
// The Program Map Table provides the mappings between program numbers and the program elements that comprise
// them.A single instance of such a mapping is referred to as a "program definition".The program map table is the
// complete collection of all program definitions for a Transport Stream.
int16_t read_pmt(uint8_t *p)
{
    uint8_t pointer_field = *p;
    inc_ptr(p, 1);
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

    program_info_length &= 0xFFF;
    
    my_printf("    <program_map_table>\n");
    my_printf("      <pointer_field>%d</pointer_field>\n", pointer_field);
    my_printf("      <table_id>0x%x</table_id>\n", table_id);
    my_printf("      <section_syntax_indicator>%d</section_syntax_indicator>\n", section_length);
    my_printf("      <section_length>%d</section_length>\n", section_length);
    my_printf("      <program_number>%d</program_number>\n", program_number);
    my_printf("      <version_number>%d</version_number>\n", version_number);
    my_printf("      <current_next_indicator>%d</current_next_indicator>\n", current_next_indicator);
    my_printf("      <section_number>%d</section_number>\n", section_number);
    my_printf("      <last_section_number>%d</last_section_number>\n", last_section_number);
    my_printf("      <pcr_pid>0x%x</pcr_pid>\n", pcr_pid);
    my_printf("      <program_info_length>%d</program_info_length>\n", program_info_length);

    p += read_descriptors(p, program_info_length);

    //my_printf("    program_number:%d, pcr_pid:%x, SCTE35:%d\n", program_number, pcr_pid, scte35_descriptor_length != 0);
    //my_printf("      Elementary Streams:\n");

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
            g_scte32_pid = elementary_pid;

        g_pid_map[elementary_pid] = g_stream_map[stream_type];

        //my_printf("        %d) pid:%x, stream_type:%x (%s)\n", stream_count++, elementary_pid, stream_type, g_stream_map[stream_type]);

        my_printf("      <stream>\n");
        my_printf("        <number>%zd</number>\n", stream_count);
        my_printf("        <pid>0x%x</pid>\n", elementary_pid);
        my_printf("        <type_number>0x%x</type_number>\n", stream_type);
        my_printf("        <type_name>%s</type_name>\n", g_stream_map[stream_type]);
        my_printf("      </stream>\n");

        stream_count++;
    }

    my_printf("    </program_map_table>\n");

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

        my_printf("      <descriptor>\n");
        my_printf("        <number>%d</number>\n", descriptor_number);
        my_printf("        <tag>%d</tag>\n", descriptor_tag);
        my_printf("        <length>%d</length>\n", descriptor_length);

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

                my_printf("        <type>video_stream_descriptor</type>\n");
                my_printf("        <multiple_frame_rate_flag>%d</multiple_frame_rate_flag>\n", multiple_frame_rate_flag);
                my_printf("        <frame_rate_code>0x%x</frame_rate_code>\n", frame_rate_code);
                my_printf("        <mpeg_1_only_flag>%d</mpeg_1_only_flag>\n", mpeg_1_only_flag);
                my_printf("        <constrained_parameter_flag>%d</constrained_parameter_flag>\n", constrained_parameter_flag);
                my_printf("        <still_picture_flag>%d</still_picture_flag>\n", still_picture_flag);

                if (!mpeg_1_only_flag)
                {
                    uint8_t profile_and_level_indication = *p;
                    inc_ptr(p, 1);
                    uint8_t chroma_format = *p;
                    inc_ptr(p, 1);
                    uint8_t frame_rate_extension_flag = (chroma_format & 0x10) >> 4;
                    chroma_format >>= 6;
                    
                    my_printf("        <profile_and_level_indication>0x%x</profile_and_level_indication>\n", profile_and_level_indication);
                    my_printf("        <chroma_format>%d</chroma_format>\n", chroma_format);
                    my_printf("        <frame_rate_extension_flag>%d</frame_rate_extension_flag>\n", frame_rate_extension_flag);
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

                my_printf("        <type>audio_stream_descriptor</type>\n");
                my_printf("        <free_format_flag>%d</free_format_flag>\n", free_format_flag);
                my_printf("        <id>%d</id>\n", id);
                my_printf("        <layer>%d</layer>\n", layer);
                my_printf("        <variable_rate_audio_indicator>%d</variable_rate_audio_indicator>\n", variable_rate_audio_indicator);
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
                my_printf("        <type>hierarchy_descriptor</type>\n");
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
                my_printf("        <type>registration_descriptor</type>\n");
                my_printf("        <format_identifier>%s</format_identifier>\n", sz_temp);
                //if (0 != scte35_format_identifier)
                //    my_printf("        <scte35_format_identifier>0x%x</scte35_format_identifier>\n", scte35_format_identifier);
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
                my_printf("        <type>data_stream_alignment_descriptor</type>\n");
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
                my_printf("        <type>target_background_grid_descriptor</type>\n");
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
                my_printf("        <type>video_window_descriptor</type>\n");
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
                my_printf("        <type>ca_descriptor</type>\n");
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
                my_printf("        <type>iso_639_language_descriptor</type>\n");
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
                my_printf("        <type>system_clock_descriptor</type>\n");
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
                my_printf("        <type>multiplex_buffer_utilization_descriptor</type>\n");
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
                my_printf("        <type>copyright_descriptor</type>\n");
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
                my_printf("        <type>maximum_bitrate_descriptor</type>\n");
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
                my_printf("        <type>private_data_indicator_descriptor</type>\n");
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
                my_printf("        <type>smoothing_buffer_descriptor</type>\n");
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
                my_printf("        <type>std_descriptor</type>\n");
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
                my_printf("        <type>ibp_descriptor</type>\n");
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
                my_printf("        <type>mpeg_4_video_descriptor</type>\n");
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
                my_printf("        <type>mpeg_4_audio_descriptor</type>\n");
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
                my_printf("        <type>iod_descriptor</type>\n");
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
                my_printf("        <type>sl_descriptor</type>\n");
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
                my_printf("        <type>fmc_descriptor</type>\n");
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
                my_printf("        <type>external_es_id_descriptor</type>\n");
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
                my_printf("        <type>muxcode_descriptor</type>\n");
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
                my_printf("        <type>fmxbuffersize_descriptor</type>\n");
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
                my_printf("        <type>multiplexbuffer_descriptor</type>\n");
            }
            break;
            default:
                inc_ptr(p, descriptor_length);
        }

        my_printf("      </descriptor>\n");

        descriptor_number++;
    }

    return p - p_descriptor_start;
}

// Process each PID for each 188 byte packet
int16_t process_pid(uint16_t pid, uint8_t *p)
{
    if(pid == 0x00)
        read_pat(p);
    else if(pid == g_program_map_pid)
        read_pmt(p);
    else
    {
        // Here, p is pointing at actual data, like video or audio.
        // For now just print the data's type.
        char *type_string = g_pid_map[pid];
        my_printf("    <type_name>%s</type_name>\n", g_pid_map[pid]);

        if(g_b_gui)
        {
            static uint16_t last_pid = 0;

            if(last_pid != pid)
            {
                pid_entry_type p(g_pid_map[pid], pid);
                g_pid_list.push_back(p);
            }

            last_pid = pid;
        }
    }

    return 0;
}

// Get the PID and other info
int16_t process_packet(uint8_t *packet, size_t packet_num)
{
    uint8_t *p = NULL;
    int16_t ret = -1;

    my_printf("  <packet>\n");
    my_printf("    <number>%zd</number>\n", packet_num);

    p = packet;

    if (0x47 != *p)
    {
        my_printf("    <error>Packet %zd does not start with 0x47</error>\n", packet_num);
        fprintf(stderr, "Error: Packet %zd does not start with 0x47\n", packet_num);
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

    // Move beyond the 32 bit header
    uint8_t final_byte = *p;
    inc_ptr(p, 1);

    uint8_t transport_scrambling_control = (final_byte & 0xC0) >> 6;
    uint8_t adaptation_field_control = (final_byte & 0x30) >> 4;
    uint8_t continuity_counter = (final_byte & 0x0F) >> 4;

    if (0x2 == adaptation_field_control ||
        0x3 == adaptation_field_control)
        assert(1);

    my_printf("    <pid>0x%x</pid>\n", PID);
    my_printf("    <transport_error_indicator>0x%x</transport_error_indicator>\n", transport_error_indicator);
    my_printf("    <payload_unit_start_indicator>0x%x</payload_unit_start_indicator>\n", payload_unit_start_indicator);
    my_printf("    <transport_priority>0x%x</transport_priority>\n", transport_priority);
    my_printf("    <transport_scrambling_control>0x%x</transport_scrambling_control>\n", transport_scrambling_control);
    my_printf("    <adaptation_field_control>0x%x</adaptation_field_control>\n", adaptation_field_control);
    my_printf("    <continuity_counter>0x%x</continuity_counter>\n", continuity_counter);

    ret = process_pid(PID, p);

process_packet_error:

    my_printf("  </packet>\n");

    return ret;
}

// It all starts here
int main(int argc, char* argv[])
{
    if (0 == argc)
    {
        fprintf(stderr, "Usage: %s [-x] [-p] mp2ts_file\n", argv[0]);
        fprintf(stderr, "-x: Output extensive xml representation of MP2TS file to stdout\n");
        fprintf(stderr, "-p: Print progress on a single line to stderr\n");
        return 0;
    }

    for (int i = 0; i < argc - 1; i++)
    {
        if (0 == strcmp("-g", argv[i]))
            g_b_gui = true;

        if (0 == strcmp("-p", argv[i]))
            g_b_progress = true;

        if(0 == strcmp("-x", argv[i]))
            g_b_xml = true;
    }

    if(g_b_gui)
    {
        unsigned int err = GLFW_NO_ERROR;

        /* Initialize the library */
        if(!glfwInit())
            return -1;

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        /* Create a windowed mode window and its OpenGL context */
        g_window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Mpeg2-TS Parser GUI", NULL, NULL);
        if (!g_window)
        {
            fprintf(stderr, "Could not create GL window! Continuing without a GUI!\n");
            glfwTerminate();
            g_b_gui = false;
            goto GUI_ERROR;
        }

        /* Make the window's context current */
        glfwMakeContextCurrent(g_window);

        glfwSwapInterval(1);

        if(glewInit() != GLEW_OK)
        {
            fprintf(stderr, "Glew Initialization Error! Continuing without a GUI!\n");
            glfwTerminate();
            g_b_gui = false;
            goto GUI_ERROR;
        }

		ImGui::CreateContext();
		ImGui_ImplGlfwGL3_Init(g_window, true);
		ImGui::StyleColorsDark();
    }

GUI_ERROR:

    uint8_t *packet_buffer, *packet;
	uint16_t program_map_pid = 0;
    size_t packet_num = 0;

	size_t packet_buffer_size = 0;
    size_t total_read = 0;
    size_t read_block_size = 0;

	FILE *f = nullptr;
	fopen_s(&f, argv[argc - 1], "rb");

	if (nullptr == f)
    {
        fprintf(stderr, "%s: Can't open input file", argv[0]);
		return -1;
    }

    // Determine the size of the file
    fseek(f, 0L, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0L, SEEK_SET);

    // Need to determine packet size.
    // Standard is 188, but digital video cameras add a 4 byte timecode
    // before the 188 byte packet, making the packet size 192.
    // https://en.wikipedia.org/wiki/MPEG_transport_stream

    uint8_t temp_buffer[5];
    fread(temp_buffer, 1, 5, f);

    uint8_t packet_size = 0;

    if(0x47 == temp_buffer[0])
        packet_size = 188;
    else if(0x47 == temp_buffer[4])
        packet_size = 192;
    else
    {
        fprintf(stderr, "%s: Can't recognize the input file", argv[0]);
        return -1;
    }

    // Go back to the beginning of the file
    fseek(f, 0L, SEEK_SET);

    if(file_size > 10000*packet_size)
        read_block_size = 10000*packet_size;
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

    my_printf("<?xml version = \"1.0\" encoding = \"UTF-8\"?>\n");
    my_printf("<file>\n");
    my_printf("  <name>%s</name>\n", argv[argc - 1]);
    my_printf("  <packet_size>%d</packet_size>\n", packet_size);

	while((size_t) (packet - packet_buffer) < packet_buffer_size)
	{
        int err = 0;

        if(192 == packet_size)
            err = process_packet(packet + 4, packet_num);
        else
            err = process_packet(packet, packet_num);

        if(0 != err)
            return err;

        total_read += packet_size;
        g_ptr_position = total_read;

        if(g_b_progress)
            fprintf(stderr, "  total_read:%zd, %2.2f%%\r", total_read, ((double)total_read / (double)file_size) * 100.);

        if(0 == (total_read % read_block_size))
        {
            packet_buffer_size = fread(packet_buffer, 1, read_block_size, f);
            packet = packet_buffer;
        }
        else
            packet += packet_size;

        assert(packet_buffer_size > 0);

        packet_num++;

        if(g_b_gui)
        {
            if(!glfwWindowShouldClose(g_window))
            {
			    glClearColor(0.f, 0.f, 0.f, 1.f);

			    ImGui_ImplGlfwGL3_NewFrame();
                /*
                if (currentTest)
			    {
				    currentTest->OnUpdate(0.f);
				    currentTest->OnRender();
				    ImGui::Begin("Test");
				    if (currentTest != testMenu && ImGui::Button("<-"))
				    {
					    delete currentTest;
					    currentTest = testMenu;
				    }
				    currentTest->OnImGuiRender();
				    ImGui::End();
			    }
                */

			    ImGui::Render();
			    ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());

                /* Swap front and back buffers */
                glfwSwapBuffers(g_window);

                /* Poll for and process events */
                glfwPollEvents();
            }
        }
    }

    my_printf("</file>\n");

    delete packet_buffer;

	fclose(f);

    if(g_b_gui)
    {
        while(!glfwWindowShouldClose(g_window))
        {
			glClearColor(0.f, 0.f, 0.f, 1.f);

			ImGui_ImplGlfwGL3_NewFrame();
/*
            if (currentTest)
			{
				currentTest->OnUpdate(0.f);
				currentTest->OnRender();
				ImGui::Begin("Test");
				if (currentTest != testMenu && ImGui::Button("<-"))
				{
					delete currentTest;
					currentTest = testMenu;
				}
				currentTest->OnImGuiRender();
				ImGui::End();
			}
*/

			ImGui::Render();
			ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());

            /* Swap front and back buffers */
            glfwSwapBuffers(g_window);

            /* Poll for and process events */
            glfwPollEvents();
        }

        ImGui_ImplGlfwGL3_Shutdown();
	    ImGui::DestroyContext();
        glfwTerminate();
    }

	return 0;
}