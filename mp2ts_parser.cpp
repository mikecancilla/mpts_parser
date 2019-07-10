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

#include "stdafx.h"
#include <stdint.h>
#include <assert.h>
#include <map>

#define READ_2_BYTES(p) *p | (*(p+1) << 8); p+=2;

#define my_printf(...) \
  { \
    if(g_b_verbose) \
      printf(__VA_ARGS__); \
  }

// Forward function definitions
size_t read_descriptors(uint8_t *p, uint16_t program_info_length);

// Type definitions
typedef std::map <uint16_t, char *> stream_map_type;
typedef std::map <uint16_t, char *> pid_map_type;

// Global definitions
stream_map_type g_stream_map;
pid_map_type g_pid_map;
bool g_b_verbose = false;
bool g_b_progress = false;
int16_t g_program_number = -1;
int16_t g_program_map_pid = -1;
int16_t g_scte32_pid = -1;

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
    uint8_t pointer_field = *p++; // Spec 2.4.4.1
    uint8_t table_id = *p++;
    uint16_t section_length = read_2_bytes(p); p += 2;
    uint8_t section_syntax_indicator = (0x8000 & section_length) >> 15;
    section_length &= 0xFFF;

    uint8_t *p_section_start = p;

    uint16_t transport_stream_id = read_2_bytes(p); p += 2;

    uint8_t current_next_indicator = *p++;
    uint8_t version_number = (current_next_indicator & 0x3E) >> 1;
    current_next_indicator &= 0x1;

    uint8_t section_number = *p++;
    uint8_t last_section_number = *p++;

    if (g_b_verbose)
    {
        printf("    <program_association_table>\n");
        printf("      <pointer_field>0x%x</pointer_field>\n", pointer_field);
        printf("      <table_id>0x%x</table_id>\n", table_id);
        printf("      <section_syntax_indicator>%d</section_syntax_indicator>\n", section_syntax_indicator);
        printf("      <section_length>%d</section_length>\n", section_length);
        printf("      <transport_stream_id>0x%x</transport_stream_id>\n", transport_stream_id);
        printf("      <version_number>0x%x</version_number>\n", version_number);
        printf("      <current_next_indicator>0x%x</current_next_indicator>\n", current_next_indicator);
        printf("      <section_number>0x%x</section_number>\n", section_number);
        printf("      <last_section_number>0x%x</last_section_number>\n", last_section_number);
    }

    while ((p - p_section_start) < (section_length - 4))
    {
        g_program_number = read_2_bytes(p); p += 2;
        uint16_t network_pid = 0;

        if (0 == g_program_number)
        {
            network_pid = read_2_bytes(p); p += 2;
            network_pid &= 0x1FFF;
        }
        else
        {
            g_program_map_pid = read_2_bytes(p); p += 2;
            g_program_map_pid &= 0x1FFF;
        }

        if (g_b_verbose)
        {
            printf("      <program>\n");
            printf("        <number>%d</number>\n", g_program_number);

            if(network_pid)
                printf("        <network_pid>0x%x</network_pid>\n", network_pid);
            else
                printf("        <program_map_pid>0x%x</program_map_pid>\n", g_program_map_pid);

            printf("      </program>\n");
        }
    }

    printf("    </program_association_table>\n");

    //my_printf("  Program Association Table:\n");
    //my_printf("    program_number:%d, program_map_id:%x\n", g_program_number, g_program_map_pid);
    return 0;
}

// 2.4.4.8 Program Map Table
//
// The Program Map Table provides the mappings between program numbers and the program elements that comprise
// them.A single instance of such a mapping is referred to as a "program definition".The program map table is the
// complete collection of all program definitions for a Transport Stream.
int16_t read_pmt(uint8_t *p)
{
    //my_printf("  Program Map Table:\n");

    uint8_t pointer_field = *p++;
    uint8_t table_id = *p++;
    uint16_t section_length = read_2_bytes(p); p += 2;
    uint8_t section_syntax_indicator = section_length & 0x80 >> 15;
    section_length &= 0xFFF;

    uint8_t *p_section_start = p;

    uint16_t program_number = read_2_bytes(p); p += 2;

    uint8_t current_next_indicator = *p++;
    uint8_t version_number = (current_next_indicator & 0x3E) >> 1;
    current_next_indicator &= 0x1;

    uint8_t section_number = *p++;
    uint8_t last_section_number = *p++;

    uint16_t pcr_pid = read_2_bytes(p); p += 2;
    pcr_pid &= 0x1FFF;

    g_pid_map[pcr_pid] = "PCR";

    uint16_t program_info_length = read_2_bytes(p); p += 2;
    program_info_length &= 0xFFF;
    
    if (g_b_verbose)
    {
        printf("    <program_map_table>\n");
        printf("      <pointer_field>%d</pointer_field>\n", pointer_field);
        printf("      <table_id>0x%x</table_id>\n", table_id);
        printf("      <section_syntax_indicator>%d</section_syntax_indicator>\n", section_length);
        printf("      <section_length>%d</section_length>\n", section_length);
        printf("      <program_number>%d</program_number>\n", program_number);
        printf("      <version_number>%d</version_number>\n", version_number);
        printf("      <current_next_indicator>%d</current_next_indicator>\n", current_next_indicator);
        printf("      <section_number>%d</section_number>\n", section_number);
        printf("      <last_section_number>%d</last_section_number>\n", last_section_number);
        printf("      <pcr_pid>0x%x</pcr_pid>\n", pcr_pid);
        printf("      <program_info_length>%d</program_info_length>\n", program_info_length);
    }

    p += read_descriptors(p, program_info_length);

    //my_printf("    program_number:%d, pcr_pid:%x, SCTE35:%d\n", program_number, pcr_pid, scte35_descriptor_length != 0);
    //my_printf("      Elementary Streams:\n");

    size_t stream_count = 0;

    // Subtract 4 from section_length to account for 4 byte CRC at its end.  The CRC is not program data.
    while((p - p_section_start) < (section_length - 4))
    {
        uint8_t stream_type = *p++;
        uint16_t elementary_pid = read_2_bytes(p); p+=2;
        elementary_pid &= 0x1FFF;

        uint16_t es_info_length = read_2_bytes(p); p += 2;
        es_info_length &= 0xFFF;

        p += es_info_length;

        // Scte35 stream type is 0x86
        if(0x86 == stream_type)
            g_scte32_pid = elementary_pid;

        g_pid_map[elementary_pid] = g_stream_map[stream_type];

        //my_printf("        %d) pid:%x, stream_type:%x (%s)\n", stream_count++, elementary_pid, stream_type, g_stream_map[stream_type]);

        if (g_b_verbose)
        {
            printf("      <stream>\n");
            printf("        <number>%zd</number>\n", stream_count);
            printf("        <pid>0x%x</pid>\n", elementary_pid);
            printf("        <type_number>0x%x</type_number>\n", stream_type);
            printf("        <type_name>%s</type_name>\n", g_stream_map[stream_type]);
            printf("      </stream>\n");
        }

        stream_count++;
    }

    if (g_b_verbose)
        printf("    </program_map_table>\n");

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
        uint8_t descriptor_tag = *p++;
        descriptor_length = *p++;

        if (g_b_verbose)
        {
            printf("      <descriptor>\n");
            printf("        <number>%d</number>\n", descriptor_number);
            printf("        <tag>%d</tag>\n", descriptor_tag);
            printf("        <length>%d</length>\n", descriptor_length);
        }

        if (VIDEO_STREAM_DESCRIPTOR == descriptor_tag)
        {
            if (g_b_verbose)
            {
                uint8_t multiple_frame_rate_flag = *p++;
                uint8_t frame_rate_code = (multiple_frame_rate_flag & 0x78) >> 3;
                uint8_t mpeg_1_only_flag = (multiple_frame_rate_flag & 0x04) >> 2;
                uint8_t constrained_parameter_flag = (multiple_frame_rate_flag & 0x02) >> 1;
                uint8_t still_picture_flag = (multiple_frame_rate_flag & 0x01);
                multiple_frame_rate_flag >>= 7;

                printf("        <type>video_stream_descriptor</type>\n");
                printf("        <multiple_frame_rate_flag>%d</multiple_frame_rate_flag>\n", multiple_frame_rate_flag);
                printf("        <frame_rate_code>0x%x</frame_rate_code>\n", frame_rate_code);
                printf("        <mpeg_1_only_flag>%d</mpeg_1_only_flag>\n", mpeg_1_only_flag);
                printf("        <constrained_parameter_flag>%d</constrained_parameter_flag>\n", constrained_parameter_flag);
                printf("        <still_picture_flag>%d</still_picture_flag>\n", still_picture_flag);

                if (!mpeg_1_only_flag)
                {
                    uint8_t profile_and_level_indication = *p++;
                    uint8_t chroma_format = *p++;
                    uint8_t frame_rate_extension_flag = (chroma_format & 0x10) >> 4;
                    chroma_format >>= 6;
                    
                    printf("        <profile_and_level_indication>0x%x</profile_and_level_indication>\n", profile_and_level_indication);
                    printf("        <chroma_format>%d</chroma_format>\n", chroma_format);
                    printf("        <frame_rate_extension_flag>%d</frame_rate_extension_flag>\n", frame_rate_extension_flag);
                }
            }
        }
        else if (AUDIO_STREAM_DESCRIPTOR == descriptor_tag)
        {
            uint8_t free_format_flag = *p++;
            uint8_t id = (free_format_flag & 0x40) >> 6;
            uint8_t layer = (free_format_flag & 0x30) >> 4;
            uint8_t variable_rate_audio_indicator = (free_format_flag & 0x08) >> 3;

            if (g_b_verbose)
            {
                printf("        <type>audio_stream_descriptor</type>\n");
                printf("        <free_format_flag>%d</free_format_flag>\n", free_format_flag);
                printf("        <id>%d</id>\n", id);
                printf("        <layer>%d</layer>\n", layer);
                printf("        <variable_rate_audio_indicator>%d</variable_rate_audio_indicator>\n", variable_rate_audio_indicator);
            }
        }
        else if(REGISTRATION_DESCRIPTOR == descriptor_tag)
        {
            uint32_t format_identifier = read_4_bytes(p); p += 4;

            if(0x43554549 == format_identifier)
                scte35_format_identifier = format_identifier; // Should be 0x43554549 (ASCII “CUEI”)

            p += descriptor_length - 4;

            if (g_b_verbose)
            {
                char sz_temp[5];
                char *p = (char *) &format_identifier;
                sz_temp[3] = *p++;
                sz_temp[2] = *p++;
                sz_temp[1] = *p++;
                sz_temp[0] = *p;
                sz_temp[4] = 0;
                printf("        <type>registration_descriptor</type>\n");
                printf("        <format_identifier>%s</format_identifier>\n", sz_temp);
                //if (0 != scte35_format_identifier)
                //    printf("        <scte35_format_identifier>0x%x</scte35_format_identifier>\n", scte35_format_identifier);
            }
        }
        else
            p += descriptor_length;

        if (g_b_verbose)
        {
            printf("      </descriptor>\n");
        }

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
        char *type_string = g_pid_map[pid];
        //assert(NULL != type_string);
        //my_printf("  PID:%x, (%s)\n", pid, type_string);
        if (g_b_verbose)
            printf("    <type_name>%s</type_name>\n", g_pid_map[pid]);
    }

    return 0;
}

// Get the PID and other info
int16_t process_packet(uint8_t *packet, size_t packet_num)
{
    uint8_t *p = NULL;
    int16_t ret = -1;

    if (g_b_verbose)
    {
        printf("  <packet>\n");
        printf("    <number>%zd</number>\n", packet_num);
    }

    p = packet;

    if (0x47 != *p)
    {
        printf("    <error>Packet %zd does not start with 0x47</error>\n", packet_num);
        fprintf(stderr, "Error: Packet %zd does not start with 0x47\n", packet_num);
        goto process_packet_error;
    }

    // Skip the sync byte 0x47
    p++;

    uint16_t PID = read_2_bytes(p); p += 2;
    uint8_t transport_error_indicator = (PID & 0x8000) >> 15;
    uint8_t payload_unit_start_indicator = (PID & 0x4000) >> 14;
    uint8_t transport_priority = (PID & 0x2000) >> 13;

    PID &= 0x1FFF;

    // Move beyond the 32 bit header
    uint8_t final_byte = *p++;
    uint8_t transport_scrambling_control = (final_byte & 0xC0) >> 6;
    uint8_t adaptation_field_control = (final_byte & 0x30) >> 4;
    uint8_t continuity_counter = (final_byte & 0x0F) >> 4;

    if (0x2 == adaptation_field_control ||
        0x3 == adaptation_field_control)
        assert(1);

    if (g_b_verbose)
    {
        printf("    <pid>0x%x</pid>\n", PID);
        printf("    <transport_error_indicator>0x%x</transport_error_indicator>\n", transport_error_indicator);
        printf("    <payload_unit_start_indicator>0x%x</payload_unit_start_indicator>\n", payload_unit_start_indicator);
        printf("    <transport_priority>0x%x</transport_priority>\n", transport_priority);
        printf("    <transport_scrambling_control>0x%x</transport_scrambling_control>\n", transport_scrambling_control);
        printf("    <adaptation_field_control>0x%x</adaptation_field_control>\n", adaptation_field_control);
        printf("    <continuity_counter>0x%x</continuity_counter>\n", continuity_counter);
    }

    ret = process_pid(PID, p);

process_packet_error:

    if (g_b_verbose)
        printf("  </packet>\n");

    return ret;
}

// It all starts here
int main(int argc, char* argv[])
{
	uint8_t *packet_buffer, *packet;
	uint16_t program_map_pid = 0;
    size_t packet_num = 0;

	size_t packet_buffer_size = 0;
    size_t total_read = 0;
    size_t read_block_size = 0;

    if (0 == argc)
    {
        fprintf(stderr, "Usage: %s [-v] [-p] mp2ts_file\n", argv[0]);
        fprintf(stderr, "-v: Output extensive xml representation of MP2TS file to stdout\n");
        fprintf(stderr, "-p: Print progress on a single line to stderr\n");
        return 0;
    }

    for (int i = 0; i < argc - 1; i++)
    {
        if(0 == strcmp("-v", argv[i]))
            g_b_verbose = true;

        if (0 == strcmp("-p", argv[i]))
            g_b_progress = true;
    }

	FILE *f = fopen(argv[argc - 1], "rb");

	if (NULL == f)
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

    if(g_b_verbose)
    {
        printf("<?xml version = \"1.0\" encoding = \"UTF-8\"?>\n");
        printf("<file>\n");
        printf("  <name>%s</name>\n", argv[argc - 1]);
        printf("  <packet_size>%d</packet_size>\n", packet_size);
    }

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
    }

    if (g_b_verbose)
        printf("</file>\n");

    delete packet_buffer;

	fclose(f);

	return 0;
}