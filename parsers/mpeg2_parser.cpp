#include <cassert>
#include "mpeg2_parser.h"
#include "utils.h"

extern size_t g_ptr_position;

static e_mpeg2_extension_type g_next_mpeg2_extension_type = sequence_extension;

static void inline inc_ptr(uint8_t *&p, size_t bytes)
{
    g_ptr_position += increment_ptr(p, bytes);
}

// MPEG2 spec, 13818-2, 6.2.2
size_t mpeg2_process_video_PES(uint8_t *&p, size_t PES_packet_data_length)
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
                bytes_processed += mpeg2_process_picture_header(p);
            break;

            case user_data_start_code:
                bytes_processed += mpeg2_process_user_data(p);
            break;

            case sequence_header_code:
                bytes_processed += mpeg2_process_sequence_header(p);
            break;

            case sequence_error_code:
            break;

            case extension_start_code:
                bytes_processed += mpeg2_process_extension(p);
            break;

            case sequence_end_code:
            break;

            case group_start_code:
                bytes_processed += mpeg2_process_group_of_pictures_header(p);
            break;

            default:
            {
                if(start_code >= slice_start_codes_begin &&
                   start_code <= slice_start_codes_end)
                {
                    bytes_processed += mpeg2_process_slice(p);
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

// MPEG2 spec, 13818-2, 6.2.2.1
size_t mpeg2_process_sequence_header(uint8_t *&p)
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

    uint32_t four_bytes = read_4_bytes(p);
    inc_ptr(p, 4);

    uint32_t time_code =  (four_bytes & 0xFFFFFF80) >> 7;
    uint8_t closed_gop =  (four_bytes & 0x00000040) >> 6;
    uint8_t broken_link = (four_bytes & 0x00000020) >> 5;

    g_next_mpeg2_extension_type = extension_and_user_data_1;

    return p - pStart;
}

size_t mpeg2_process_picture_header(uint8_t *&p)
{
    uint8_t *pStart = p;

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

size_t mpeg2_process_user_data(uint8_t *&p)
{
    uint8_t *pStart = p;

    while(    *p != 0 ||
          *(p+1) != 0 ||
          *(p+2) != 1)
        p++;

    return p - pStart;
}

size_t mpeg2_process_slice(uint8_t *&p)
{
    uint8_t *pStart = p;

    return p - pStart;
}
