#pragma once

#include <cstdint>

/*
    Taken from 6.2.2 Video Sequence

    Certain extensions are valid only at certain times.
    They all follow after the standard extension_start_code of 0x000001B5.
    The TYPE of data that follows the extension_strt_code is context dependent.
*/
enum e_mpeg2_extension_type
{
    sequence_extension = 0,
    picture_coding_extension,
    extension_and_user_data_0,
    extension_and_user_data_1,
    extension_and_user_data_2,
    extension_unknown
};

/*
    Table 6-1

    Reserved: B0, B1, B6
*/
enum e_mpeg2_start_code
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

/*
    Table 6-2

    Reserved: 0, 4, 6, 11, 12, 13, 14, 15
*/
enum e_mpeg2_extension_start_code_identifier
{
    sequence_extension_id = 1,
    sequence_display_extension_id = 2,
    quant_matrix_extension = 3,
    sequence_scalable_extension_id = 5,
    picture_display_extension_id = 7,
    picture_coding_extension_id = 8,
    picture_spatial_scalable_extension_id = 9,
    picture_temporal_scalable_extension_id = 10
};

size_t mpeg2_process_video_PES(uint8_t *&p, size_t PES_packet_data_length);
size_t mpeg2_process_sequence_header(uint8_t *&p);
size_t mpeg2_process_extension(uint8_t *&p);
size_t mpeg2_process_sequence_display_extension(uint8_t *&p);
size_t mpeg2_process_sequence_scalable_extension(uint8_t *&p);
size_t mpeg2_process_extension_and_user_data_0(uint8_t *&p);
size_t mpeg2_process_group_of_pictures_header(uint8_t *&p);
size_t mpeg2_process_picture_header(uint8_t *&p);
size_t mpeg2_process_picture_coding_extension(uint8_t *&p);
size_t mpeg2_process_user_data(uint8_t *&p);
size_t mpeg2_process_slice(uint8_t *&p);
