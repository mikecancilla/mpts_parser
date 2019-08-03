#include <cstdio>
#include <cassert>
#include <cstring> // memcpy
#include "mpeg2_parser.h"
#include "utils.h"

static e_mpeg2_extension_type g_next_mpeg2_extension_type = sequence_extension;
static bool g_b_xml_out = false;

static void inline printf_xml(unsigned int indent_level, const char *format, ...)
{
    if(format)
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

static void inline inc_ptr(uint8_t *&p, size_t bytes)
{
    increment_ptr(p, bytes);
}

static size_t mpeg2_next_start_code(uint8_t *&p, size_t data_length = -1)
{
    size_t count = 0;
    uint8_t *pStart = p;

    while(    *p != 0 ||
          *(p+1) != 0 ||
          *(p+2) != 1 &&
          count < data_length)
    {
        inc_ptr(p, 1);
        count++;
    }

    if(-1 == count)
        return count;

    return p - pStart;
}

static inline size_t mpeg2_skip_to_next_start_code(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint32_t four_bytes = read_4_bytes(p);
    inc_ptr(p, 4);

    mpeg2_next_start_code(p);

    return p - pStart;
}

static inline size_t mpeg2_process_start_code(uint8_t *&p, e_mpeg2_start_code start_code)
{
    uint32_t four_bytes = read_4_bytes(p);
    inc_ptr(p, 4);

    uint32_t start_code_prefix = (four_bytes & 0xFFFFFF00) >> 8;
    assert(0x000001 == start_code_prefix);

    four_bytes &= 0x000000FF;
    assert(four_bytes == start_code);

    return 4;
}

static uint32_t read_time_stamp(uint8_t *&p)
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

size_t mpeg2_process_video_frames(uint8_t *p, size_t PES_packet_data_length, unsigned int how_many_frames, bool b_xml_out)
{
    g_b_xml_out = b_xml_out;

    uint8_t *pStart = p;
    size_t bytes_processed = 0;
    bool bDone = false;
    unsigned int frame_number = 0;

    while(bytes_processed < PES_packet_data_length && !bDone)
    {
RETRY:
        uint32_t start_code = read_4_bytes(p);
        uint32_t start_code_prefix = (start_code & 0xFFFFFF00) >> 8;

        if(0x000001 != start_code_prefix)
        {
            fprintf(stderr, "WARNING: Bad data found %llu bytes into this frame.  Searching for next start code...\n", bytes_processed);
            size_t count = mpeg2_next_start_code(p, PES_packet_data_length);

            if(-1 == count)
            {
                bDone = true;
                continue;
            }

            goto RETRY;
        }

        start_code &= 0x000000FF;
        switch(start_code)
        {
            case picture_start_code:
                bytes_processed += mpeg2_process_picture_header(p);
            break;

            case user_data_start_code:
                //bytes_processed += mpeg2_process_user_data(p);
                bytes_processed += mpeg2_skip_to_next_start_code(p);
            break;

            case sequence_header_code:
                //bytes_processed += mpeg2_process_sequence_header(p);
                bytes_processed += mpeg2_skip_to_next_start_code(p);
            break;

            case sequence_error_code:
                bDone = true;
            break;

            case extension_start_code:
                //bytes_processed += mpeg2_process_extension(p);
                bytes_processed += mpeg2_skip_to_next_start_code(p);
            break;

            case sequence_end_code:
                bDone = true;
            break;

            case group_start_code:
                //bytes_processed += mpeg2_process_group_of_pictures_header(p);
                bytes_processed += mpeg2_skip_to_next_start_code(p);
            break;

            default:
            {
                if(start_code >= slice_start_codes_begin &&
                   start_code <= slice_start_codes_end)
                {
                    //bytes_processed += mpeg2_process_slice(p);
                    bytes_processed += mpeg2_skip_to_next_start_code(p);
                }
                else if(start_code >= system_start_codes_begin &&
                        start_code <= system_start_codes_end)
                {
                    if(frame_number == how_many_frames)
                    {
                        bDone = true;
                    }
                    else
                    {
                        //bytes_processed += mpeg2_process_PES_packet_header(p);
                        bytes_processed += mpeg2_skip_to_next_start_code(p);
                        frame_number++;
                    }
                }
            }
        }
    }

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2
size_t mpeg2_process_video_PES(uint8_t *p, size_t PES_packet_data_length)
{
    uint8_t *pStart = p;
    size_t bytes_processed = 0;
    bool bDone = false;

    while(bytes_processed < PES_packet_data_length && !bDone)
    {
        bytes_processed += mpeg2_process_video_frames(p, PES_packet_data_length, 1);
    }

    return p - pStart;
}

// http://dvd.sourceforge.net/dvdinfo/pes-hdr.html
size_t mpeg2_process_PES_packet_header(uint8_t *&p)
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

// MPEG2 spec, 13818-2, 6.2.2.1
size_t mpeg2_process_sequence_header(uint8_t *&p)
{
    uint8_t *pStart = p;

    mpeg2_process_start_code(p, sequence_header_code);

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

    g_next_mpeg2_extension_type = sequence_extension;

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.3
size_t mpeg2_process_sequence_extension(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint32_t four_bytes = read_4_bytes(p);
    inc_ptr(p, 4);

    e_mpeg2_extension_start_code_identifier extension_start_code_identifier = (e_mpeg2_extension_start_code_identifier) ((four_bytes & 0xF0000000) >> 28);
    assert(sequence_extension_id == extension_start_code_identifier);

    uint8_t profile_and_level_indication = (four_bytes & 0x0FF00000) >> 20;
    uint8_t progressive_sequence = (four_bytes & 0x00080000) >> 19;
    uint8_t chroma_format = (four_bytes & 0x00060000) >> 17;
    uint8_t horizontal_size_extension = (four_bytes & 0x00018000) >> 15;
    uint8_t vertical_size_extension = (four_bytes & 0x00006000) >> 13;
    uint16_t bit_rate_extension = (four_bytes & 0x00001FFE) >> 1;
    // marker_bit

    uint8_t vbv_buffer_size_extension = *p;
    inc_ptr(p, 1);

    uint8_t byte = *p;
    inc_ptr(p, 1);

    uint8_t low_delay = (byte & 0x80) >> 7;
    uint8_t frame_rate_extension_n = (byte & 0x60) >> 5;
    uint8_t frame_rate_extension_d = byte & 0x1F;

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.4
size_t mpeg2_process_sequence_display_extension(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint8_t byte = *p;
    inc_ptr(p, 1);

    uint8_t video_format = (byte & 0x0E) >> 1;
    uint8_t colour_description = byte & 0x01;

    if(colour_description)
    {
        uint8_t colour_primaries = *p;
        inc_ptr(p, 1);

        uint8_t transfer_characteristics = *p;
        inc_ptr(p, 1);

        uint8_t matrix_coefficients = *p;
        inc_ptr(p, 1);
    }

    uint32_t four_bytes = read_4_bytes(p);
    inc_ptr(p, 4);

    uint16_t display_horizontal_size = (four_bytes & 0xFFFC0000) >> 18;
    // marker_bit
    uint16_t display_vertical_size =   (four_bytes & 0x0001FFF8) >> 3;

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.5
size_t mpeg2_process_sequence_scalable_extension(uint8_t *&p)
{
    uint8_t *pStart = p;

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.2.1
size_t mpeg2_process_extension_and_user_data_0(uint8_t *&p)
{
    uint8_t *pStart = p;

    if(sequence_display_extension_id == ((*p & 0xF0) >> 4))
        mpeg2_process_sequence_display_extension(p);

    if(sequence_scalable_extension_id == ((*p & 0xF0) >> 4))
        mpeg2_process_sequence_scalable_extension(p);

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.2.1
//
// The setting of g_next_mpeg2_extension_type follows the diagram of 6.2.2 Video Sequence
size_t mpeg2_process_extension(uint8_t *&p)
{
    uint8_t *pStart = p;

    mpeg2_process_start_code(p, extension_start_code);

    switch(g_next_mpeg2_extension_type)
    {
        case sequence_extension:
            mpeg2_process_sequence_extension(p);
            g_next_mpeg2_extension_type = extension_and_user_data_0;
        break;
        
        case picture_coding_extension:
            mpeg2_process_picture_coding_extension(p);
            g_next_mpeg2_extension_type = extension_and_user_data_2;
        break;

        case extension_and_user_data_0:
            mpeg2_process_extension_and_user_data_0(p);

            //    The next extension can be either:
            //        extension_and_user_data_1 (Follows a GOP)
            //        extension_and_user_data_2 (Follows a picture_coding_extension)
            g_next_mpeg2_extension_type = extension_unknown;
        break;

        case extension_and_user_data_1:
        break;

        case extension_and_user_data_2:
        break;
    }

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.6
size_t mpeg2_process_group_of_pictures_header(uint8_t *&p)
{
    uint8_t *pStart = p;

    mpeg2_process_start_code(p, group_start_code);

    uint32_t four_bytes = read_4_bytes(p);
    inc_ptr(p, 4);

    uint32_t time_code =  (four_bytes & 0xFFFFFF80) >> 7;
    uint8_t closed_gop =  (four_bytes & 0x00000040) >> 6;
    uint8_t broken_link = (four_bytes & 0x00000020) >> 5;

    g_next_mpeg2_extension_type = extension_and_user_data_1;

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.3
size_t mpeg2_process_picture_header(uint8_t *&p)
{
    uint8_t *pStart = p;

    mpeg2_process_start_code(p, picture_start_code);

    uint32_t four_bytes = read_4_bytes(p);
    inc_ptr(p, 4);
    
    uint16_t temporal_reference = (four_bytes & 0xFFC00000) >> 22;
    uint8_t picture_coding_type = (four_bytes & 0x00380000) >> 19;
    uint16_t vbv_delay =          (four_bytes & 0x0007FFF8) >> 3;

    uint8_t carry_over = four_bytes & 0x07;
    uint8_t carry_over_bits = 3;
    uint8_t full_pel_forward_vector = 0;
    uint8_t forward_f_code = 0;
    uint8_t full_pel_backward_vector = 0;
    uint8_t backward_f_code = 0;

    printf_xml(2, "<frame type>%c</frame type>\n", " IPB"[picture_coding_type]);

    if(2 == picture_coding_type)
    {
        full_pel_forward_vector = (four_bytes & 0x04) >> 2;
        forward_f_code = (four_bytes & 0x03) << 2;

        carry_over = *p;
        inc_ptr(p, 1);

        forward_f_code |= (carry_over & 0x80) >> 7;

        carry_over &= 0x7F;
        carry_over_bits = 7;
    }
    else if(3 == picture_coding_type)
    {
        full_pel_forward_vector = (four_bytes & 0x04) >> 2;
        forward_f_code = (four_bytes & 0x03) << 2;

        carry_over = *p;
        inc_ptr(p, 1);

        forward_f_code |= (carry_over & 0x80) >> 7;

        full_pel_backward_vector = (carry_over & 0x40) >> 6;
        backward_f_code = (carry_over & 0x38) >> 3;

        carry_over &= 0x03;
        carry_over_bits = 2;
    }

    if(carry_over & (0x01 << (carry_over_bits-1)))
        assert(1); // TODO: Handle this case

    g_next_mpeg2_extension_type = picture_coding_extension;

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.3.1
size_t mpeg2_process_picture_coding_extension(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint32_t four_bytes = read_4_bytes(p);
    inc_ptr(p, 4);

    e_mpeg2_extension_start_code_identifier extension_start_code_identifier = (e_mpeg2_extension_start_code_identifier) ((four_bytes & 0xF0000000) >> 28);
    assert(picture_coding_extension_id == extension_start_code_identifier);

    uint8_t f_code[4];
    f_code[0] = (four_bytes & 0x0F000000);
    f_code[1] = (four_bytes & 0x00F00000);
    f_code[2] = (four_bytes & 0x000F0000);
    f_code[3] = (four_bytes & 0x0000F000);

    uint8_t intra_dc_precision         = (four_bytes & 0x00000C00) >> 10;
    uint8_t picture_structure          = (four_bytes & 0x00000300) >> 8;
    uint8_t top_field_first            = (four_bytes & 0x00000080) >> 7;
    uint8_t frame_pred_frame_dct       = (four_bytes & 0x00000040) >> 6;
    uint8_t concealment_motion_vectors = (four_bytes & 0x00000020) >> 5;
    uint8_t q_scale_type               = (four_bytes & 0x00000010) >> 4;
    uint8_t intra_vlc_format           = (four_bytes & 0x00000008) >> 3;
    uint8_t alternate_scan             = (four_bytes & 0x00000004) >> 2;
    uint8_t repeat_first_field         = (four_bytes & 0x00000002) >> 1;
    uint8_t chroma_420_type            = four_bytes & 0x00000001;

    uint32_t byte = *p;
    inc_ptr(p, 1);

    uint8_t progressive_frame      = (byte & 0x80) >> 7;
    uint8_t composite_display_flag = (byte & 0x40) >> 6;

    if(composite_display_flag)
    {
        uint8_t v_axis             = (byte & 0x20) >> 5;
        uint8_t field_sequence     = (byte & 0x1C) >> 2;
        uint8_t sub_carrier        = (byte & 0x02) >> 1;
        uint8_t burst_amplitude    = (byte & 0x01) << 6;

        byte = *p;
        inc_ptr(p, 1);

        burst_amplitude |= (byte & 0xFC) >> 2;

        uint8_t sub_carrier_phase = (byte & 0x03) << 6;

        byte = *p;
        inc_ptr(p, 1);

        sub_carrier_phase |= (byte & 0xFC) >>2;
    }

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.2.2
size_t mpeg2_process_user_data(uint8_t *&p)
{
    uint8_t *pStart = p;

    mpeg2_process_start_code(p, user_data_start_code);

    mpeg2_next_start_code(p);

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.4
size_t mpeg2_process_slice(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint32_t four_bytes = read_4_bytes(p);
    inc_ptr(p, 4);

    uint32_t start_code_prefix = (four_bytes & 0xFFFFFF00) >> 8;
    assert(0x000001 == start_code_prefix);

    uint8_t slice_number = four_bytes & 0xff;

    mpeg2_next_start_code(p);

    return p - pStart;
}
