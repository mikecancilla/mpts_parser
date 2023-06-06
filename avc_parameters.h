#pragma once

#include <stdint.h>
#include <vector>

struct AccessUnitDelimiter
{
    uint8_t primary_pic_type;
};

struct SchedSelIdx
{
    SchedSelIdx(uint32_t bit_rate_value_minus1,
                uint32_t cpb_size_value_minus1,
                uint8_t cbr_flag)
        : bit_rate_value_minus1(bit_rate_value_minus1) // 0 to 2^32 − 2, inclusive
        , cpb_size_value_minus1(cpb_size_value_minus1) // 0 to 2^32 − 2, inclusive
        , cbr_flag(cbr_flag) {}

    uint32_t bit_rate_value_minus1;
    uint32_t cpb_size_value_minus1;
    uint8_t cbr_flag;
};

// E.1.2 HRD parameters syntax
struct HrdParameters
{
    uint8_t cpb_cnt_minus1; //0 to 31 inclusive
    uint8_t bit_rate_scale;
    uint8_t cpb_size_scale;

    std::vector<SchedSelIdx> sched_sel_idx;

    uint8_t initial_cpb_removal_delay_length_minus1;
    uint8_t cpb_removal_delay_length_minus1;
    uint8_t dpb_output_delay_length_minus1;
    uint8_t time_offset_length;
};

// E.1.1 VUI parameters syntax
struct VuiParameters
{
    uint8_t aspect_ratio_info_present_flag;
    uint8_t aspect_ratio_idc;
    uint16_t sar_width;
    uint16_t sar_height;
    uint8_t overscan_info_present_flag;
    uint8_t overscan_appropriate_flag;
    uint8_t video_signal_type_present_flag;
    uint8_t video_format;
    uint8_t video_full_range_flag;
    uint8_t colour_description_present_flag;
    uint8_t colour_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coefficients;
    uint8_t chroma_loc_info_present_flag;
    uint16_t chroma_sample_loc_type_top_field;
    uint16_t chroma_sample_loc_type_bottom_field;
    uint8_t timing_info_present_flag;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    uint8_t fixed_frame_rate_flag;

    uint8_t nal_hrd_parameters_present_flag;
    HrdParameters nal_hrd_parameters;

    uint8_t vcl_hrd_parameters_present_flag;
    HrdParameters vcl_hrd_parameters;

    uint8_t low_delay_hrd_flag;
    uint8_t pic_struct_present_flag;
    uint8_t bitstream_restriction_flag;
    uint8_t motion_vectors_over_pic_boundaries_flag;
    uint16_t max_bytes_per_pic_denom;
    uint16_t max_bits_per_mb_denom;
    uint16_t log2_max_mv_length_horizontal;
    uint16_t log2_max_mv_length_vertical;
    uint16_t max_num_reorder_frames;
    uint16_t max_dec_frame_buffering;
};

// 7.3.2.1.1 Sequence parameter set data syntax
struct SequenceParameterSet
{
    uint8_t profile_idc;
    uint8_t constraint_set0_flag;
    uint8_t constraint_set1_flag;
    uint8_t constraint_set2_flag;
    uint8_t constraint_set3_flag;
    uint8_t constraint_set4_flag;
    uint8_t constraint_set5_flag;
    uint8_t level_idc;
    uint8_t seq_parameter_set_id; // 0 to 31, inclusive
    uint8_t chroma_format_idc;
    uint8_t separate_colour_plane_flag;
    uint8_t bit_depth_luma_minus8;
    uint8_t bit_depth_chroma_minus8;
    uint8_t qpprime_y_zero_transform_bypass_flag;
    uint8_t seq_scaling_matrix_present_flag;

    std::vector<bool> seq_scaling_list_present_flag;

    // Look at scaling_list() here

    uint8_t log2_max_frame_num_minus4;
    uint8_t pic_order_cnt_type;
    uint8_t log2_max_pic_order_cnt_lsb_minus4; // 0 to 12, inclusive
    uint8_t delta_pic_order_always_zero_flag;
    int32_t offset_for_non_ref_pic; // −2^31 + 1 to 2^31 − 1, inclusive
    int32_t offset_for_top_to_bottom_field; // −2^31 + 1 to 2^31 − 1, inclusive
    uint8_t num_ref_frames_in_pic_order_cnt_cycle; // 0-255

    std::vector<int32_t> offset_for_ref_frame; // −2^31 + 1 to 2^31 − 1, inclusive

    uint16_t max_num_ref_frames; // 0 to MaxDpbFrames
    uint8_t gaps_in_frame_num_value_allowed_flag;
    uint16_t pic_width_in_mbs_minus1; // 0 to MaxDpbFrames
    uint16_t pic_height_in_map_units_minus1;
    uint8_t frame_mbs_only_flag;
    uint8_t mb_adaptive_frame_field_flag;
    uint8_t direct_8x8_inference_flag;
    uint8_t frame_cropping_flag;
    uint8_t frame_crop_left_offset;
    uint8_t frame_crop_right_offset;
    uint8_t frame_crop_top_offset;
    uint8_t frame_crop_bottom_offset;
    uint8_t vui_parameters_present_flag;

    VuiParameters vui_parameters;
};

struct PictureParameterSet
{

};

struct SeiMessage
{
};

// 7.3.3 Bit stream syntax
// 7.3.4 Slice header semantics
struct SliceHeader
{
    uint8_t first_mb_in_slice;
    uint8_t slice_type;
    uint8_t pic_parameter_set_id;
    uint8_t colour_plane_id;
    uint32_t frame_num;
    uint8_t field_pic_flag;
    uint8_t bottom_field_flag;
    uint8_t idr_pic_id;
    uint32_t pic_order_cnt_lsb;
    int delta_pic_order_cnt_bottom;
    std::vector<int> delta_pic_order_cnt; // 2 element array
    uint8_t direct_spatial_mv_pred_flag;
    uint8_t num_ref_idx_active_override_flag;
    uint8_t num_ref_idx_l0_active_minus1;
    uint8_t num_ref_idx_l1_active_minus1;

    //ref_pic_list_mvc_modification() /* specified in Annex H */
    //ref_pic_list_modification()
    //pred_weight_table()
    //dec_ref_pic_marking()

    uint8_t cabac_init_idc;
    int slice_qp_delta;
    uint8_t sp_for_switch_flag;
    int slice_qs_delta;
    uint8_t disable_deblocking_filter_idc;
    int slice_alpha_c0_offset_div2;
    int slice_beta_offset_div2;
    uint8_t slice_group_change_cycle;
};

struct NALData
{
    int picture_type;
    AccessUnitDelimiter access_unit_delimiter;
    SequenceParameterSet sequence_parameter_set;
    PictureParameterSet picture_parameter_set;
    SliceHeader slice_header; // only the first one for now
};
