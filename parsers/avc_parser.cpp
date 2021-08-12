#include "avc_parser.h"
#include <cmath>
#include "utils.h"
#include "bit_stream.h"

static void printf_xml(unsigned int indent_level, const char *format, ...)
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

size_t avc_parser::process_video_frames(uint8_t *p, size_t PES_packet_data_length, unsigned int frames_wanted, unsigned int &frames_received, bool b_xml_out)
{
    m_b_xml_out = b_xml_out;

    uint8_t *packet_start = p;
    size_t bytes_processed = 0;
    bool bDone = false;
    frames_received = 0;

    while((size_t) (p - packet_start) < PES_packet_data_length && !bDone)
    {
        uint32_t four_bytes = read_4_bytes(p);
        increment_ptr(p, 4);

        int64_t NumBytesInNALunit = 0;
        if(0x00000001 == four_bytes)
        {
            uint8_t *p_nalu_start = p;
            bool bFound = false;

            // We have an AnnexB NALU, find the next 0x00000001 and count bytes
//            while ((size_t)(p - packet_start + 4) < PES_packet_data_length && !bFound) {
            while ((size_t)(p - packet_start) < (PES_packet_data_length - 4) && !bFound)
            {
                four_bytes = read_4_bytes(p);

                if (0x00000001 == four_bytes)
                    bFound = true;
                else
                    increment_ptr(p, 1);
            }

            NumBytesInNALunit = p - p_nalu_start;
            
            // If we did not find a NALU start code, then all the rest of the data belongs to this frame
            if (!bFound)
                NumBytesInNALunit += 4; // Take into consideration the 4 bytes we don't eat

            p = p_nalu_start; // reset p to start of nalu
        }
        else
            NumBytesInNALunit = four_bytes;

        if (0 == NumBytesInNALunit)
            continue;

        // H.264 spec, 7.3.1 NAL unit syntax

        uint8_t *p_nalu_data_start = p;

        uint8_t byte = *p;
        increment_ptr(p, 1);

        assert(0 == (byte & 0x80)); // Forbidden zero bit

        uint8_t nal_ref_idc = byte & 0x60 >> 5;
        eAVCNaluType nal_unit_type = (eAVCNaluType)(byte & 0x1f);

        uint32_t NumBytesInRBSP = 0;
        uint32_t nalUnitHeaderBytes = 1;

        if (nal_unit_type == eAVCNaluType_PrefixNalUnit || nal_unit_type == eAVCNaluType_CodedSliceExtension)
        {
            nalUnitHeaderBytes += 3;
            p += 3;
        }

        //while(p - p_nalu_data_start < NumBytesInNALunit)
        //{
        if ((p+2 - p_nalu_data_start) < NumBytesInNALunit)
        {
            uint32_t emulation_prevention_three_byte = (*p << 16) | (*(p+1) << 8) | *(p+2);

            if (0x03 == emulation_prevention_three_byte)
            {
                p += 3;
                continue;
            }
        }

        switch (nal_unit_type)
        {
            case eAVCNaluType_AccessUnitDelimiter:
                process_access_unit_delimiter(p);
                //p += NumBytesInNALunit - (p - p_nalu_data_start);
                break;

            case eAVCNaluType_SequenceParameterSet:
                process_sequence_parameter_set(p);
                //p += NumBytesInNALunit - (p - p_nalu_data_start);
                break;

            case eAVCNaluType_PictureParameterSet:
                process_picture_parameter_set(p);
                //p += NumBytesInNALunit - (p - p_nalu_data_start);
                break;

            case eAVCNaluType_SupplementalEnhancementInformation:
                process_sei_message(p, p_nalu_data_start + NumBytesInNALunit);
                //p += NumBytesInNALunit - (p - p_nalu_data_start);
                break;

            case eAVCNaluType_CodedSliceIdrPicture:
                printf_xml(2, "<closed_gop>%d</closed_gop>\n", 1);

            case eAVCNaluType_CodedSliceAuxiliaryPicture:
            case eAVCNaluType_CodedSliceNonIdrPicture:
                process_slice_layer_without_partitioning(p);
                //p += NumBytesInNALunit - (p - p_nalu_data_start);
                frames_received++;
                bDone = true;
                break;
        }
        //}

        p = p_nalu_data_start + NumBytesInNALunit;
    }

    return p - packet_start;
}

// 7.3.2.3.1 Supplemental enhancement information message syntax
// Annex D - SEI Messages
size_t avc_parser::process_sei_message(uint8_t*& p, uint8_t* pLastByte)
{
    uint8_t *pStart = p;

    while (p < pLastByte)
    {
        uint32_t payloadType = 0;
        uint8_t *payloadStart = p;

        while (*p == 0xFF)
        {
            increment_ptr(p, 1);
            payloadType += 255;
        }

        uint8_t last_payload_type_byte = *p;
        increment_ptr(p, 1);

        payloadType += last_payload_type_byte;

        uint32_t payloadSize = 0;

        while (*p == 0xFF)
        {
            increment_ptr(p, 1);
            payloadSize += 255;
        }

        uint8_t last_payload_size_byte = *p;
        increment_ptr(p, 1);

        payloadSize += last_payload_size_byte;

        if (6 == payloadType)
            process_recovery_point_sei(p);
        else
            p += payloadSize;

        //p = payloadStart + payloadSize;
    }

    return p - pStart;
}

size_t avc_parser::process_recovery_point_sei(uint8_t*& p)
{
    uint8_t* pStart = p;

    uint32_t bitsRead = 0;
    BitStream bs(p);
    uint16_t recovery_frame_cnt = EGParse(bs, bitsRead);
    uint8_t exact_match_flag = bs.GetBits(1);
    uint8_t broken_link_flag = bs.GetBits(1);
    uint8_t changing_slice_group_idc = bs.GetBits(2);

    return p - pStart;
}

size_t avc_parser::process_slice_layer_without_partitioning(uint8_t*& p)
{
    uint8_t* pStart = p;

    process_slice_header(p);

    // process_slice_data()
    // rbsp_slice_trailing_bits()

    return p - pStart;
}

// 7.4.3
size_t avc_parser::process_slice_header(uint8_t*& p)
{
    uint8_t *pStart = p;

    BitStream bs(p);

    uint32_t bitsRead = 0;
    uint16_t first_mb_in_slice = EGParse(bs, bitsRead);
    uint8_t slice_type = EGParse(bs, bitsRead);

    /*
    Table 7-6 – Name association to slice_type
    slice_type      Name of slice_type
    0               P (P slice)
    1               B (B slice)
    2               I (I slice)
    3               SP (SP slice)
    4               SI (SI slice)
    5               P (P slice)
    6               B (B slice)
    7               I (I slice)
    8               SP (SP slice)
    9               SI (SI slice)
    */

    printf_xml(2, "<type>%c</type>\n", "PBIPIPBIPI"[slice_type]);

    return p - pStart;
}

size_t avc_parser::process_picture_parameter_set(uint8_t *&p)
{
    uint8_t *pStart = p;
    return p - pStart;
}

// 7.3.2.1.1 - Table
// 7.4.2.1.1 Sequence parameter set data semantics
size_t avc_parser::process_sequence_parameter_set(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint8_t byte = *p;
    increment_ptr(p, 1);

    uint8_t profile_idc = byte;

    byte = *p;
    increment_ptr(p, 1);

    uint8_t constraint_set0_flag = (byte & 0x80) >> 7;
    uint8_t constraint_set1_flag = (byte & 0x40) >> 6;
    uint8_t constraint_set2_flag = (byte & 0x20) >> 5;
    uint8_t constraint_set3_flag = (byte & 0x10) >> 4;
    uint8_t constraint_set4_flag = (byte & 0x08) >> 3;
    uint8_t constraint_set5_flag = (byte & 0x04) >> 2;
    // reserved_zero_2_bits

    byte = *p;
    increment_ptr(p, 1);

    uint8_t level_idc = byte;

    BitStream bs(p);

    uint32_t bitsRead = 0;
    uint8_t seq_parameter_set_id = EGParse(bs, bitsRead); // 0 to 31, inclusive

    if (44 == profile_idc ||
        83 == profile_idc ||
        86 == profile_idc ||
        100 == profile_idc ||
        110 == profile_idc ||
        118 == profile_idc ||
        122 == profile_idc ||
        128 == profile_idc ||
        224 == profile_idc)
    {
    }

    uint8_t log2_max_frame_num_minus4 = EGParse(bs, bitsRead);
    uint8_t pic_order_cnt_type = EGParse(bs, bitsRead);

    if (0 == pic_order_cnt_type)
    {
        uint8_t log2_max_pic_order_cnt_lsb_minus4 = EGParse(bs, bitsRead); // 0 to 12, inclusive
    }
    else if (1 == pic_order_cnt_type)
    {
        uint8_t delta_pic_order_always_zero_flag = bs.GetBits(1);
        int32_t offset_for_non_ref_pic = EGParse(bs, bitsRead); // −2^31 + 1 to 2^31 − 1, inclusive
        int32_t offset_for_top_to_bottom_field = EGParse(bs, bitsRead); // −2^31 + 1 to 2^31 − 1, inclusive
        uint8_t num_ref_frames_in_pic_order_cnt_cycle = EGParse(bs, bitsRead); // 0-255

        for (int i=0; i<num_ref_frames_in_pic_order_cnt_cycle; i++)
        {
            // TODO: Create an array
            int32_t offest_for_ref_frame = EGParse(bs, bitsRead); // −2^31 + 1 to 2^31 − 1, inclusive
        }
    }

    uint16_t max_num_ref_frames = EGParse(bs, bitsRead); // 0 to MaxDpbFrames
    uint8_t gaps_in_frame_num_value_allowed_flag = bs.GetBits(1);
    uint16_t pic_width_in_bms_minus1 = EGParse(bs, bitsRead); // 0 to MaxDpbFrames
    uint16_t pic_height_in_map_units_minus1 = EGParse(bs, bitsRead);
    uint8_t frame_mbs_only_flag = bs.GetBits(1);

    if (0 == frame_mbs_only_flag)
    {
        uint8_t mb_adaptive_frame_field_flag = bs.GetBits(1);
    }

    uint8_t direct_8x8_inference_flag = bs.GetBits(1);
    uint8_t frame_cropping_flag = bs.GetBits(1);

    if (frame_cropping_flag)
    {
        uint8_t frame_crop_left_offset = EGParse(bs, bitsRead);
        uint8_t frame_crop_right_offset = EGParse(bs, bitsRead);
        uint8_t frame_crop_top_offset = EGParse(bs, bitsRead);
        uint8_t frame_crop_bottom_offset = EGParse(bs, bitsRead);
    }

    uint8_t vui_parameters_present_flag = bs.GetBits(1);

    if (vui_parameters_present_flag)
        process_vui_parameters(bs);

    return bs.m_p - pStart;
}

size_t avc_parser::process_vui_parameters(BitStream& bs)
{
    return 0;
}

size_t avc_parser::process_access_unit_delimiter(uint8_t*& p)
{
    uint8_t* pStart = p;

    uint8_t primary_pic_type = (*p & 0xE0) >> 5;
    increment_ptr(p, 1);

    return p - pStart;
}

// Exp-Golomb Parse, Clause 9.1
uint8_t avc_parser::EGParse(BitStream &bs, uint32_t &bitsRead)
{
    (void)bitsRead;
    uint8_t codeNum = 0;
    int8_t leadingZeroBits = -1;
    uint8_t b = 0;

    for (b=0; !b; ++leadingZeroBits)
    {
        b = bs.GetBits(1);
    }

    codeNum = static_cast<uint8_t>(std::pow(2, leadingZeroBits) - 1);
    codeNum += bs.GetBits(leadingZeroBits);

    return codeNum;
}

/*
uint8_t avc_parser::EGParse(BitStream &bs, uint32_t &bitsRead)
{
    uint8_t ret = 0;
    uint8_t numZeroes = 0;
    uint8_t bit = 0;

    while (!bit)
    {
        bit = bs.GetBits(1);

        if (!bit)
            numZeroes++;
    }

    ret = std::pow(2, numZeroes) - 1;
    ret += bs.GetBits(numZeroes);

    if (numZeroes)
        bitsRead = numZeroes * 2;
    else
        bitsRead = 0;

    return ret;
}
*/