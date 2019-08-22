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
    SCTE 35 in MPTS
    http://www.scte.org/SCTEDocs/Standards/SCTE%2035%202016.pdf
*/

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstdarg>
#include <vector>
#include <map>
//#include "tinyxml2.h"

#include "mpts_parser.h"
#include "utils.h"
#include "mpeg2_parser.h"

#define VIDEO_DATA_MEMORY_INCREMENT (500 * 1024)

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

mpts_parser::mpts_parser(size_t &file_position)
    : m_p_video_data(NULL)
    , m_file_position(file_position)
    , m_packet_size(0)
    , m_program_number(-1)
    , m_program_map_pid(-1)
    , m_network_pid(0x0010)
    , m_scte35_pid(-1)
    , m_video_data_size(0)
    , m_video_buffer_size(0)
    , m_b_xml(true)
    , m_b_terse(true)
    , m_b_analyze_elementary_stream(false)
{
}

mpts_parser::~mpts_parser()
{
    pop_video_data();
}

bool mpts_parser::set_print_xml(bool tf)
{
    bool ret = m_b_xml;
    m_b_xml = tf;
    return ret;
}

bool mpts_parser::get_print_xml()
{
    return m_b_xml;
}

bool mpts_parser::set_terse(bool tf)
{
    bool ret = m_b_terse;
    m_b_terse = tf;
    return ret;
}

bool mpts_parser::get_terse()
{
    return m_b_terse;
}

bool mpts_parser::set_analyze_elementary_stream(bool tf)
{
    bool ret = m_b_analyze_elementary_stream;
    m_b_analyze_elementary_stream = tf;
    return ret;
}

bool mpts_parser::get_analyze_elementary_stream()
{
    return m_b_analyze_elementary_stream;
}

void inline mpts_parser::printf_xml(unsigned int indent_level, const char *format, ...)
{
    if(m_b_xml && format)
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

void inline mpts_parser::inc_ptr(uint8_t *&p, size_t bytes)
{
    m_file_position += increment_ptr(p, bytes);
}

// Initialize a map of ID to string type
void mpts_parser::init_stream_types(std::map <uint16_t, char *> &stream_map)
{
    stream_map[0x0] = "Reserved";	                            
    stream_map[0x1] = "MPEG-1 Video";
    stream_map[0x2] = "MPEG-2 Video";
    stream_map[0x3] = "MPEG-1 Audio";
    stream_map[0x4] = "MPEG-2 Audio";
    stream_map[0x5] = "ISO 13818-1 private sections";
    stream_map[0x6] = "ISO 13818-1 PES private data";
    stream_map[0x7] = "ISO 13522 MHEG";
    stream_map[0x8] = "ISO 13818-1 DSM - CC";
    stream_map[0x9] = "ISO 13818-1 auxiliary";
    stream_map[0xa] = "ISO 13818-6 multi-protocol encap";
    stream_map[0xb] = "ISO 13818-6 DSM-CC U-N msgs";
    stream_map[0xc] = "ISO 13818-6 stream descriptors";
    stream_map[0xd] = "ISO 13818-6 sections";
    stream_map[0xe] = "ISO 13818-1 auxiliary";
    stream_map[0xf] = "MPEG-2 AAC Audio";
    stream_map[0x10] = "MPEG-4 Video";
    stream_map[0x11] = "MPEG-4 LATM AAC Audio";
    stream_map[0x12] = "MPEG-4 generic";
    stream_map[0x13] = "ISO 14496-1 SL-packetized";
    stream_map[0x14] = "ISO 13818-6 Synchronized Download Protocol";
    stream_map[0x1b] = "H.264 Video";
    stream_map[0x80] = "DigiCipher II Video";
    stream_map[0x81] = "A52 / AC-3 Audio";
    stream_map[0x82] = "HDMV DTS Audio";
    stream_map[0x83] = "LPCM Audio";
    stream_map[0x84] = "SDDS Audio";
    stream_map[0x85] = "ATSC Program ID";
    stream_map[0x86] = "DTS-HD Audio";
    stream_map[0x87] = "E-AC- 3 Audio";
    stream_map[0x8a] = "DTS Audio";
    stream_map[0x91] = "A52b / AC-3 Audio";
    stream_map[0x92] = "DVD_SPU vls Subtitle";
    stream_map[0x94] = "SDDS Audio";
    stream_map[0xa0] = "MSCODEC Video";
    stream_map[0xea] = "Private ES(VC-1)";
}

size_t mpts_parser::push_video_data(uint8_t *p, size_t size)
{
    if(m_video_data_size + size > m_video_buffer_size)
    {
        m_video_buffer_size += VIDEO_DATA_MEMORY_INCREMENT;
        m_p_video_data = (uint8_t*) realloc((void*) m_p_video_data, m_video_buffer_size);
    }

    std::memcpy(m_p_video_data + m_video_data_size, p, size);
    m_video_data_size += size;

    return m_video_data_size;
}

// Returns the amount of bytes left in the video buffer after compacting
size_t mpts_parser::compact_video_data(size_t bytes_to_compact)
{
    size_t bytes_leftover = m_video_data_size - bytes_to_compact;

    if(bytes_leftover > 0)
    {
        std::memcpy(m_p_video_data, m_p_video_data + bytes_to_compact, bytes_leftover);
        m_video_data_size = bytes_leftover;
    }

    return bytes_leftover;
}

size_t mpts_parser::get_video_data_size()
{
    return m_video_data_size;
}

size_t mpts_parser::pop_video_data()
{
    size_t ret = m_video_data_size;
    if(m_p_video_data)
    {
        free(m_p_video_data);
        m_p_video_data = NULL;
    }

    m_video_data_size = 0;
    m_video_buffer_size = 0;
    
    return ret;
}

// 2.4.4.3 Program association Table
//
// The Program Association Table provides the correspondence between a program_number and the PID value of the
// Transport Stream packets which carry the program definition.The program_number is the numeric label associated with
// a program.
int16_t mpts_parser::read_pat(uint8_t *&p, bool payload_unit_start)
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
        m_program_number = read_2_bytes(p);
        inc_ptr(p, 2);
        uint16_t network_pid = 0;

        if (0 == m_program_number)
        {
            network_pid = read_2_bytes(p);
            inc_ptr(p, 2);
            network_pid &= 0x1FFF;
            m_network_pid = network_pid;
        }
        else
        {
            m_program_map_pid = read_2_bytes(p);
            inc_ptr(p, 2);
            m_program_map_pid &= 0x1FFF;
        }

        printf_xml(3, "<program>\n");
        printf_xml(4, "<number>%d</number>\n", m_program_number);

        if(network_pid)
            printf_xml(4, "<network_pid>0x%x</network_pid>\n", m_network_pid);
        else
            printf_xml(4, "<program_map_pid>0x%x</program_map_pid>\n", m_program_map_pid);

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
int16_t mpts_parser::read_pmt(uint8_t *&p, bool payload_unit_start)
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

    m_pid_map[pcr_pid] = "PCR";

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

    //my_printf("program_number:%d, pcr_pid:%x\n", program_number, pcr_pid);
    //my_printf("  Elementary Streams:\n");

    std::map <uint16_t, char *> stream_map; // ID, name
    init_stream_types(stream_map);

    // This has to be done by hand
    m_pid_map[0x1FFF] = "NULL Packet";

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
            m_scte35_pid = elementary_pid;

        m_pid_map[elementary_pid] = stream_map[stream_type];
        m_pid_to_type_map[elementary_pid] = (mpts_e_stream_type) stream_type;

        //my_printf("    %d) pid:%x, stream_type:%x (%s)\n", stream_count++, elementary_pid, stream_type, stream_map[stream_type]);

        printf_xml(3, "<stream>\n");
        printf_xml(4, "<number>%zd</number>\n", stream_count);
        printf_xml(4, "<pid>0x%x</pid>\n", elementary_pid);
        printf_xml(4, "<type_number>0x%x</type_number>\n", stream_type);
        printf_xml(4, "<type_name>%s</type_name>\n", stream_map[stream_type]);
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
size_t mpts_parser::read_descriptors(uint8_t *p, uint16_t program_info_length)
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

// http://dvd.sourceforge.net/dvdinfo/pes-hdr.html
size_t mpts_parser::process_PES_packet_header(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint32_t four_bytes = read_4_bytes(p);
    inc_ptr(p, 4);

    uint32_t packet_start_code_prefix = (four_bytes & 0xffffff00) >> 8;
    uint8_t stream_id = four_bytes & 0xff;

    /* 2.4.3.7
      PES_packet_length – A 16-bit field specifying the number of bytes in the PES packet following the last byte of the field.
      A value of 0 indicates that the PES packet length is neither specified nor bounded and is allowed only in
      PES packets whose payload consists of bytes from a video elementary stream contained in Transport Stream packets.
    */

    int64_t PES_packet_length = read_2_bytes(p+4);
    inc_ptr(p, 2);

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

    static uint64_t PTS_last = 0;

    if(2 == PTS_DTS_flags)
    {
        uint64_t PTS = read_time_stamp(p);
        printf_xml(2, "<PTS>%llu (%f)</PTS>\n", PTS, convert_time_stamp(PTS));
    }

    static uint64_t DTS_last = 0;

    if(3 == PTS_DTS_flags)
    {
        uint64_t PTS = read_time_stamp(p);
        uint64_t DTS = read_time_stamp(p);

        printf_xml(2, "<DTS>%llu (%f)</DTS>\n", DTS, convert_time_stamp(DTS));
        printf_xml(2, "<PTS>%llu (%f)</PTS>\n", PTS, convert_time_stamp(PTS));
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
            std::memcpy(PES_private_data, p, 16);
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

    /*
        From the TS spec:
        stuffing_byte – This is a fixed 8-bit value equal to '1111 1111' that can be inserted by the encoder, for example to meet
        the requirements of the channel. It is discarded by the decoder. No more than 32 stuffing bytes shall be present in one
        PES packet header.
    */

    while(*p == 0xFF)
    {
        p++;
        inc_ptr(p,1);
    }

    return p - pStart;
}

// Push data into video buffer for later processing by a decoder
size_t mpts_parser::process_PES_packet(uint8_t *&p, int64_t packet_start_in_file, mpts_e_stream_type stream_type, bool payload_unit_start)
{
    if(false == payload_unit_start)
    {
        size_t PES_packet_data_length = m_packet_size - (m_file_position - packet_start_in_file);
        
//        if(g_b_analyze_elementary_stream)
//            push_video_data(p, PES_packet_data_length);

        inc_ptr(p, PES_packet_data_length);
        return PES_packet_data_length;
    }

    // Peek at the next 6 bytes to figure out stream_id
    uint32_t four_bytes = read_4_bytes(p);

    uint32_t packet_start_code_prefix = (four_bytes & 0xffffff00) >> 8;
    uint8_t stream_id = four_bytes & 0xff;

    /* 2.4.3.7
      PES_packet_length – A 16-bit field specifying the number of bytes in the PES packet following the last byte of the field.
      A value of 0 indicates that the PES packet length is neither specified nor bounded and is allowed only in
      PES packets whose payload consists of bytes from a video elementary stream contained in Transport Stream packets.
    */

    int64_t PES_packet_length = read_2_bytes(p+4);

    if(0 == PES_packet_length)
        PES_packet_length = m_packet_size - (m_file_position - packet_start_in_file);

    if (stream_id != program_stream_map &&
        stream_id != padding_stream &&
        stream_id != private_stream_2 &&
        stream_id != ECM_stream &&
        stream_id != EMM_stream &&
        stream_id != program_stream_directory &&
        stream_id != DSMCC_stream &&
        stream_id != itu_h222_e_stream)
    {
        if(eMPEG2_Video == stream_type)
        {
            // Push first PES packet, lots of info here.
            if(m_b_analyze_elementary_stream)
                push_video_data(p, PES_packet_length);

            inc_ptr(p, PES_packet_length);
        }
        else
        {
            inc_ptr(p, PES_packet_length);
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

// Process each PID (Packet Identifier) for each 188 byte packet
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

int16_t mpts_parser::process_pid(uint16_t pid, uint8_t *&p, int64_t packet_start_in_file, size_t packet_num, bool payload_unit_start)
{
    if(0x00 == pid) // PAT - Program Association Table
    {
        static bool g_b_want_pat = true;

        if(g_b_want_pat)
        {
            if(m_b_terse)
            {
                printf_xml(1, "<packet start=\"%llu\">\n", packet_start_in_file);
                printf_xml(2, "<number>%zd</number>\n", packet_num);
                printf_xml(2, "<pid>0x%x</pid>\n", pid);
                printf_xml(2, "<payload_unit_start_indicator>0x%x</payload_unit_start_indicator>\n", payload_unit_start ? 1 : 0);
            }

            read_pat(p, payload_unit_start);

            if(m_b_terse)
                printf_xml(1, "</packet>\n");
        }

        if(m_b_terse)
            g_b_want_pat = false;
    }
    else if(0x01 == pid) // CAT - Conditional Access Table
    {
    }
    else if(0x02 == pid) // TSDT - Transport Stream Description Table
    {
    }
    else if(0x03 == pid) // IPMP
    {
    }
    else if(0x04 <= pid &&
            0x0F >= pid) // Reserved
    {
    }
    else if(0x10 <= pid &&
            0x1F >= pid) // DVB metadata
    {
    }
    else if(m_program_map_pid == pid)
    {
        static bool g_b_want_pmt = true;

        if(g_b_want_pmt)
        {
            if(m_b_terse)
            {
                printf_xml(1, "<packet start=\"%llu\">\n", packet_start_in_file);
                printf_xml(2, "<number>%zd</number>\n", packet_num);
                printf_xml(2, "<pid>0x%x</pid>\n", pid);
                printf_xml(2, "<payload_unit_start_indicator>0x%x</payload_unit_start_indicator>\n", payload_unit_start ? 1 : 0);
            }

            read_pmt(p, payload_unit_start);

            if(m_b_terse)
                printf_xml(1, "</packet>\n");
        }

        if(m_b_terse)
            g_b_want_pmt = false;
    }
    else
    {
        if(false == m_b_terse)
        {
            // Here, p is pointing at actual data, like video or audio.
            // For now just print the data's type.
            printf_xml(2, "<type_name>%s</type_name>\n", m_pid_map[pid]);
        }
        else
        {
            static mpts_frame videoFrame;
            static mpts_frame audioFrame;
            static size_t lastPid = -1;

            mpts_frame *p_frame = nullptr;

            switch(m_pid_to_type_map[pid])
            {
                case eMPEG2_Video:
                    p_frame = &videoFrame;
                    p_frame->pid = pid;
                    p_frame->streamType = eMPEG2_Video;
                break;
                case eMPEG1_Video:
                case eMPEG4_Video:
                case eH264_Video:
                case eDigiCipher_II_Video:
                case eMSCODEC_Video:
                break;

                case eMPEG1_Audio:
                case eMPEG2_Audio:
                case eMPEG2_AAC_Audio:
                case eMPEG4_LATM_AAC_Audio:
                case eA52_AC3_Audio:
                case eHDMV_DTS_Audio:
                case eA52b_AC3_Audio:
                case eSDDS_Audio:
                    //p_frame = &audioFrame;
                    //p_frame->pid = pid;
                break;
            }

            if(p_frame)
            {
                bool bNewSet = false;

                if(payload_unit_start)
                {
                    if(p_frame->pidList.size())
                    {
                        for(mpts_pid_list_type::size_type i = 0; i != p_frame->pidList.size(); i++)
                            p_frame->totalPackets += p_frame->pidList[i].num_packets;

                        printf_xml(1,
                                   "<frame number=\"%d\" name=\"%s\" packets=\"%d\" pid=\"0x%x\">\n",
                                   p_frame->frameNumber++, p_frame->pidList[0].pid_name.c_str(), p_frame->totalPackets, pid);

                        if(m_b_analyze_elementary_stream)
                        {
                            unsigned int frames_received = 0;
                            size_t bytes_processed = process_video_frames(m_p_video_data, m_video_data_size, 1, frames_received, m_b_xml);
                            //compact_video_data(bytes_processed);
                            pop_video_data();
                        }

                        printf_xml(2, "<slices>\n");

                        for(mpts_pid_list_type::size_type i = 0; i != p_frame->pidList.size(); i++)
                            printf_xml(3, "<slice byte=\"%llu\" packets=\"%d\"/>\n", p_frame->pidList[i].pid_byte_location, p_frame->pidList[i].num_packets);

                        printf_xml(2, "</slices>\n");

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
                    mpts_pid_entry_type pet(m_pid_map[pid], 1, packet_start_in_file);
                    p_frame->pidList.push_back(pet);
                }
                else
                {
                    mpts_pid_entry_type &pet = p_frame->pidList.back();
                    pet.num_packets++;
                }
            }

            lastPid = pid;
        }

        process_PES_packet(p, packet_start_in_file, m_pid_to_type_map[pid], payload_unit_start);
    }

    return 0;
}

uint8_t mpts_parser::process_adaptation_field(unsigned int indent, uint8_t *&p)
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
int16_t mpts_parser::process_packet(uint8_t *packet, size_t packetNum)
{
    uint8_t *p = NULL;
    int16_t ret = 0;
    int64_t packet_start_in_file = m_file_position;

    if(false == m_b_terse)
    {
        printf_xml(1, "<packet start=\"%llu\">\n", m_file_position);
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

    // Move beyond the 32 bit header
    uint8_t final_byte = *p;
    inc_ptr(p, 1);

    uint8_t transport_scrambling_control = (final_byte & 0xC0) >> 6;
    adaptation_field_control = (final_byte & 0x30) >> 4;
    uint8_t continuity_counter = (final_byte & 0x0F) >> 4;

    if(false == m_b_terse)
    {
        printf_xml(2, "<pid>0x%x</pid>\n", PID);
        printf_xml(2, "<payload_unit_start_indicator>0x%x</payload_unit_start_indicator>\n", payload_unit_start_indicator);
        printf_xml(2, "<transport_error_indicator>0x%x</transport_error_indicator>\n", transport_error_indicator);
        printf_xml(2, "<transport_priority>0x%x</transport_priority>\n", transport_priority);
        printf_xml(2, "<transport_scrambling_control>0x%x</transport_scrambling_control>\n", transport_scrambling_control);
        printf_xml(2, "<adaptation_field_control>0x%x</adaptation_field_control>\n", adaptation_field_control);
        printf_xml(2, "<continuity_counter>0x%x</continuity_counter>\n", continuity_counter);
    }

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

    if(2 != adaptation_field_control)
        ret = process_pid(PID, p, packet_start_in_file, packetNum, 1 == payload_unit_start_indicator);

process_packet_error:

    if(false == m_b_terse)
        printf_xml(1, "</packet>\n");

    return ret;
}

// 2.4.3.6 PES Packet
//
// Return a 33 bit number representing the time stamp
uint64_t mpts_parser::read_time_stamp(uint8_t *&p)
{
    uint64_t byte = *p;
    inc_ptr(p, 1);

    uint64_t time_stamp = (byte & 0x0E) << 29;

    uint64_t two_bytes = read_2_bytes(p);
    inc_ptr(p, 2);

    time_stamp |= (two_bytes & 0xFFFE) << 14;

    two_bytes = read_2_bytes(p);
    inc_ptr(p, 2);

    time_stamp |= (two_bytes & 0xFFFE) >> 1;

    return time_stamp;
}

float mpts_parser::convert_time_stamp(uint64_t time_stamp)
{
    return (float) time_stamp / 90000.f;
}

size_t mpts_parser::process_video_frames(uint8_t *p, size_t PES_packet_data_length, unsigned int frames_wanted, unsigned int &frames_received, bool b_xml_out)
{
    uint8_t *pStart = p;
    size_t bytes_processed = 0;
    bool bDone = false;
    frames_received = 0;

    while(bytes_processed < PES_packet_data_length && !bDone)
    {
RETRY:
        uint32_t start_code = read_4_bytes(p);
        uint32_t start_code_prefix = (start_code & 0xFFFFFF00) >> 8;

        if(0x000001 != start_code_prefix)
        {
            fprintf(stderr, "WARNING: Bad data found %llu bytes into this frame.  Searching for next start code...\n", bytes_processed);
            size_t count = next_start_code(p, PES_packet_data_length);

            if(-1 == count)
            {
                bDone = true;
                continue;
            }

            goto RETRY;
        }

        start_code &= 0x000000FF;

        if(start_code >= system_start_codes_begin &&
           start_code <= system_start_codes_end)
        {
            if(frames_received == frames_wanted)
            {
                bDone = true;
            }
            else
            {
                bytes_processed += process_PES_packet_header(p);
                //bytes_processed += skip_to_next_start_code(p);
            }

            continue;
        }

        bytes_processed = mpeg2_process_video_frames(p, PES_packet_data_length, frames_wanted, frames_received, b_xml_out);
        if(frames_wanted == frames_received)
            bDone = true;
    }

    return p - pStart;
}

// Is this mpts from an OTA broadcast (188 byte packets) or a BluRay (192 byte packets)?
// See: https://github.com/lerks/BluRay/wiki/M2TS
int mpts_parser::determine_packet_size(uint8_t buffer[5])
{
    if(0x47 == buffer[0])
        m_packet_size = 188;
    else if(0x47 == buffer[4])
        m_packet_size = 192;
    else
        return -1;

    return m_packet_size;
}
