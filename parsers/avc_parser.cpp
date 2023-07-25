#include "avc_parser.h"
#include <cmath>
#include "util.h"
#include "bit_stream.h"

ProcessNaluResult avcParser::processNalu(uint8_t* p,
                                         size_t dataLength,
                                         NALData& nalData)
{
    uint8_t* packetStart = p;
    size_t bytesProcessed = 0;
    bool bDone = false;

    ProcessNaluResult ret;

    while ((size_t)(p - packetStart) < dataLength - 3 && !bDone)
    {
        // See section: B.2 Byte stream NAL unit decoding process
        // Eat leading_zero_8bits and trailing_zero_8bits with this loop
        uint32_t threeBytes = util::read3Bytes(p);
        while (0x000001 != threeBytes && (size_t)(p - packetStart) < dataLength - 4)
        {
            util::incrementPtr(p, 1);
            threeBytes = util::read3Bytes(p);
        }

        util::incrementPtr(p, 3);

        int64_t NumBytesInNALunit = 0;
        if (0x000001 == threeBytes)
        {
            uint8_t* pNaluStart = p;
            bool bFound = false;

            // We have an AnnexB NALU, find the next 0x00000001 and count bytes
            while ((size_t)(p - packetStart) < (dataLength - 4) && !bFound)
            {
                threeBytes = util::read3Bytes(p);

                // B.2 Point 3
                if (0x000000 == threeBytes ||
                    0x000001 == threeBytes)
                    bFound = true;
                else
                    util::incrementPtr(p, 1);
            }

            NumBytesInNALunit = p - pNaluStart;

            // If we did not find a NALU start code, then all the rest of the data belongs to this frame
            if (!bFound)
                NumBytesInNALunit += 4; // Take into consideration the 4 bytes we don't eat

            p = pNaluStart; // reset p to start of nalu
        }
        // Is this else necessary/legal?
        //else
        //    NumBytesInNALunit = fourBytes;

        if (0 == NumBytesInNALunit)
            continue;

        // H.264 spec, 7.3.1 NAL unit syntax

        uint8_t* pNaluDataStart = p;

        uint8_t byte = *p;
        util::incrementPtr(p, 1);

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

        if ((p + 2 - pNaluDataStart) < NumBytesInNALunit)
        {
            uint32_t emulation_prevention_three_byte = (*p << 16) | (*(p + 1) << 8) | *(p + 2);

            if (0x03 == emulation_prevention_three_byte)
            {
                p += 3;
                continue;
            }
        }

        switch (nal_unit_type)
        {
        case eAVCNaluType_AccessUnitDelimiter:
            processAccessUnitDelimiter(p, nalData.access_unit_delimiter);
            ret.result = eAVCNaluType_AccessUnitDelimiter;
            bDone = true;
            break;

        case eAVCNaluType_SequenceParameterSet:
            processSequenceParameterSet(p, nalData.sequence_parameter_set);
            ret.result = eAVCNaluType_SequenceParameterSet;
            bDone = true;
            break;

        case eAVCNaluType_PictureParameterSet:
            processPictureParameterSet(p, nalData.picture_parameter_set);
            ret.result = eAVCNaluType_PictureParameterSet;
            bDone = true;
            break;

        case eAVCNaluType_SupplementalEnhancementInformation:
            processSeiMessage(p, pNaluDataStart + NumBytesInNALunit);
            ret.result = eAVCNaluType_SupplementalEnhancementInformation;
            bDone = true;
            break;

        case eAVCNaluType_CodedSliceIdrPicture:
            ret.result = eAVCNaluType_CodedSliceIdrPicture;
            bDone = true;
            break;

        case eAVCNaluType_CodedSliceAuxiliaryPicture:
            processSliceLayerWithoutPartitioning(p, nalData.slice_header, nalData.sequence_parameter_set);
            ret.result = eAVCNaluType_CodedSliceAuxiliaryPicture;
            bDone = true;
            break;

        case eAVCNaluType_CodedSliceNonIdrPicture:
            processSliceLayerWithoutPartitioning(p, nalData.slice_header, nalData.sequence_parameter_set);
            ret.result = eAVCNaluType_CodedSliceNonIdrPicture;
            bDone = true;
            break;

        default:
            break;
        }

        p = pNaluDataStart + NumBytesInNALunit;
    }

    ret.bytes = p - packetStart;

    return ret;
}

size_t avcParser::processVideoFrame(uint8_t* p,
    size_t dataLength,
    std::any& returnData)
{
    uint8_t* packetStart = p;
    NALData* pNalData = std::any_cast<NALData*>(returnData);

    ProcessNaluResult naluResult;

    while (eAVCNaluType_CodedSliceIdrPicture != naluResult.result &&
           eAVCNaluType_CodedSliceAuxiliaryPicture != naluResult.result &&
           eAVCNaluType_CodedSliceNonIdrPicture != naluResult.result)
    {
        naluResult = processNalu(p,
                                 dataLength,
                                 *pNalData);

        p += naluResult.bytes;
        dataLength -= naluResult.bytes;
    }

    pNalData->picture_type = naluResult.result;

    return p - packetStart;
}

// 7.3.2.3.1 Supplemental enhancement information message syntax
// Annex D - SEI Messages
size_t avcParser::processSeiMessage(uint8_t*& p, uint8_t* pLastByte)
{
    uint8_t *pStart = p;

    while (p < pLastByte)
    {
        uint32_t payloadType = 0;
        uint8_t *payloadStart = p;

        while (*p == 0xFF)
        {
            util::incrementPtr(p, 1);
            payloadType += 255;
        }

        uint8_t last_payload_type_byte = *p;
        util::incrementPtr(p, 1);

        payloadType += last_payload_type_byte;

        uint32_t payloadSize = 0;

        while (*p == 0xFF)
        {
            util::incrementPtr(p, 1);
            payloadSize += 255;
        }

        uint8_t last_payload_size_byte = *p;
        util::incrementPtr(p, 1);

        payloadSize += last_payload_size_byte;

        if (6 == payloadType)
            processRecoveryPointSei(p);
        else
            p += payloadSize;

        //p = payloadStart + payloadSize;
    }

    return p - pStart;
}

size_t avcParser::processRecoveryPointSei(uint8_t*& p)
{
    uint8_t* pStart = p;

    BitStream bs(p);
    uint16_t recovery_frame_cnt = UEGParse(bs);
    uint8_t exact_match_flag = bs.GetBits(1);
    uint8_t broken_link_flag = bs.GetBits(1);
    uint8_t changing_slice_group_idc = bs.GetBits(2);

    return p - pStart;
}

size_t avcParser::processSliceLayerWithoutPartitioning(uint8_t*& p, SliceHeader& sliceHeader, const SequenceParameterSet& sps)
{
    uint8_t* pStart = p;

    processSliceHeader(p, sliceHeader, sps);

    // process_slice_data()
    // rbsp_slice_trailing_bits()

    return p - pStart;
}

size_t avcParser::processSliceLayerWithoutPartitioning(uint8_t*& p)
{
    uint8_t* pStart = p;

    processSliceHeader(p);

    // process_slice_data()
    // rbsp_slice_trailing_bits()

    return p - pStart;
}

// 7.3.3 Bit stream syntax
// 7.3.4 Slice header semantics
size_t avcParser::processSliceHeader(uint8_t*& p, SliceHeader& sliceHeader, const SequenceParameterSet& sps)
{
    uint8_t* pStart = p;

    BitStream bs(p);

    sliceHeader.first_mb_in_slice = UEGParse(bs);
    sliceHeader.slice_type = UEGParse(bs);
    sliceHeader.pic_parameter_set_id = UEGParse(bs);

    return p - pStart;
}

// 7.3.3 Bit stream syntax
// 7.3.4 Slice header semantics
size_t avcParser::processSliceHeader(uint8_t*& p)
{
    uint8_t *pStart = p;

    BitStream bs(p);

    uint8_t first_mb_in_slice = UEGParse(bs);
    uint8_t slice_type = UEGParse(bs);

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

    // TODO: this slice header data needs to be in the NALData struct
    util::printfXml(2, "<type>%c</type>\n", "PBIPIPBIPI"[slice_type]);

    return p - pStart;
}

size_t avcParser::processPictureParameterSet(uint8_t*& p, PictureParameterSet& pps)
{
    uint8_t* pStart = p;
    return p - pStart;
}

size_t avcParser::processPictureParameterSet(uint8_t *&p)
{
    util::printfXml(2, "<PPS>\n");
    util::printfXml(2, "</PPS>\n");

    uint8_t *pStart = p;
    return p - pStart;
}

// 7.3.2.1.1 - Table
// 7.4.2.1.1 Sequence parameter set data semantics
size_t avcParser::processSequenceParameterSet(uint8_t*& p, SequenceParameterSet& sps)
{
    uint8_t* pStart = p;

    sps = { 0 };

    uint8_t byte = *p;
    util::incrementPtr(p, 1);

    sps.profile_idc = byte;

    byte = *p;
    util::incrementPtr(p, 1);

    sps.constraint_set0_flag = (byte & 0x80) >> 7;
    sps.constraint_set1_flag = (byte & 0x40) >> 6;
    sps.constraint_set2_flag = (byte & 0x20) >> 5;
    sps.constraint_set3_flag = (byte & 0x10) >> 4;
    sps.constraint_set4_flag = (byte & 0x08) >> 3;
    sps.constraint_set5_flag = (byte & 0x04) >> 2;

    // reserved_zero_2_bits

    byte = *p;
    util::incrementPtr(p, 1);

    sps.level_idc = byte;

    BitStream bs(p);

    sps.seq_parameter_set_id = UEGParse(bs); // 0 to 31, inclusive

    if (44 == sps.profile_idc ||
        83 == sps.profile_idc ||
        86 == sps.profile_idc ||
        100 == sps.profile_idc ||
        110 == sps.profile_idc ||
        118 == sps.profile_idc ||
        122 == sps.profile_idc ||
        128 == sps.profile_idc ||
        134 == sps.profile_idc ||
        135 == sps.profile_idc ||
        138 == sps.profile_idc ||
        139 == sps.profile_idc ||
        244 == sps.profile_idc)
    {
        sps.chroma_format_idc = UEGParse(bs);

        if (3 == sps.chroma_format_idc)
        {
            sps.separate_colour_plane_flag = bs.GetBits(1);
        }

        sps.bit_depth_luma_minus8 = UEGParse(bs);
        sps.bit_depth_chroma_minus8 = UEGParse(bs);
        sps.qpprime_y_zero_transform_bypass_flag = bs.GetBits(1);
        sps.seq_scaling_matrix_present_flag = bs.GetBits(1);

        if (sps.seq_scaling_matrix_present_flag)
        {
            int count = sps.chroma_format_idc != 3 ? 8 : 12;
            for (int i = 0; i < count; i++)
            {
                sps.seq_scaling_list_present_flag.push_back(bs.GetBits(1) ? true : false);

                /* TODO
                if (seq_scaling_list_present_flag[i])
                {
                    if (i < 6)
                    {
                        scaling_list(ScalingList4x4[i], 16, UseDefaultScalingMatrix4x4Flag[i])
                    }
                    else
                    {
                        scaling_list(ScalingList8x8[i − 6], 64, UseDefaultScalingMatrix8x8Flag[i − 6])
                    }
                }
                */
            }
        }
    }

    sps.log2_max_frame_num_minus4 = UEGParse(bs);
    sps.pic_order_cnt_type = UEGParse(bs);

    if (0 == sps.pic_order_cnt_type)
    {
        sps.log2_max_pic_order_cnt_lsb_minus4 = UEGParse(bs); // 0 to 12, inclusive
    }
    else if (1 == sps.pic_order_cnt_type)
    {
        sps.delta_pic_order_always_zero_flag = bs.GetBits(1);
        sps.offset_for_non_ref_pic = SEGParse(bs); // −2^31 + 1 to 2^31 − 1, inclusive
        sps.offset_for_top_to_bottom_field = SEGParse(bs); // −2^31 + 1 to 2^31 − 1, inclusive
        sps.num_ref_frames_in_pic_order_cnt_cycle = UEGParse(bs); // 0-255

        for (int i = 0; i < sps.num_ref_frames_in_pic_order_cnt_cycle; i++)
        {
            // TODO: Create an array
            sps.offset_for_ref_frame.push_back(SEGParse(bs)); // −2^31 + 1 to 2^31 − 1, inclusive
        }
    }

    sps.max_num_ref_frames = UEGParse(bs); // 0 to MaxDpbFrames
    sps.gaps_in_frame_num_value_allowed_flag = bs.GetBits(1);
    sps.pic_width_in_mbs_minus1 = UEGParse(bs); // 0 to MaxDpbFrames
    sps.pic_height_in_map_units_minus1 = UEGParse(bs);
    sps.frame_mbs_only_flag = bs.GetBits(1);

    if (0 == sps.frame_mbs_only_flag)
    {
        sps.mb_adaptive_frame_field_flag = bs.GetBits(1);
    }

    sps.direct_8x8_inference_flag = bs.GetBits(1);
    sps.frame_cropping_flag = bs.GetBits(1);

    if (sps.frame_cropping_flag)
    {
        sps.frame_crop_left_offset = UEGParse(bs);
        sps.frame_crop_right_offset = UEGParse(bs);
        sps.frame_crop_top_offset = UEGParse(bs);
        sps.frame_crop_bottom_offset = UEGParse(bs);
    }

    sps.vui_parameters_present_flag = bs.GetBits(1);

    if (sps.vui_parameters_present_flag)
        processVuiParameters(bs, sps.vui_parameters);

    return bs.m_p - pStart;
}

// 7.3.2.1.1 - Table
// 7.4.2.1.1 Sequence parameter set data semantics
size_t avcParser::processSequenceParameterSet(uint8_t*& p)
{
    util::printfXml(2, "<SPS>\n");

    uint8_t* pStart = p;

    uint8_t byte = *p;
    util::incrementPtr(p, 1);

    uint8_t profile_idc = byte;
    util::printfXml(3, "<profile_idc>%d</profile_idc>\n", profile_idc);

    byte = *p;
    util::incrementPtr(p, 1);

    uint8_t constraint_set0_flag = (byte & 0x80) >> 7;
    util::printfXml(3, "<constraint_set0_flag>%d</constraint_set0_flag>\n", constraint_set0_flag);
    uint8_t constraint_set1_flag = (byte & 0x40) >> 6;
    util::printfXml(3, "<constraint_set1_flag>%d</constraint_set1_flag>\n", constraint_set1_flag);
    uint8_t constraint_set2_flag = (byte & 0x20) >> 5;
    util::printfXml(3, "<constraint_set2_flag>%d</constraint_set2_flag>\n", constraint_set2_flag);
    uint8_t constraint_set3_flag = (byte & 0x10) >> 4;
    util::printfXml(3, "<constraint_set3_flag>%d</constraint_set3_flag>\n", constraint_set3_flag);
    uint8_t constraint_set4_flag = (byte & 0x08) >> 3;
    util::printfXml(3, "<constraint_set4_flag>%d</constraint_set4_flag>\n", constraint_set4_flag);
    uint8_t constraint_set5_flag = (byte & 0x04) >> 2;
    util::printfXml(3, "<constraint_set5_flag>%d</constraint_set5_flag>\n", constraint_set5_flag);
    // reserved_zero_2_bits

    byte = *p;
    util::incrementPtr(p, 1);

    uint8_t level_idc = byte;
    util::printfXml(3, "<level_idc>%d</level_idc>\n", level_idc);

    BitStream bs(p);

    uint8_t seq_parameter_set_id = UEGParse(bs); // 0 to 31, inclusive
    util::printfXml(3, "<seq_parameter_set_id>%d</seq_parameter_set_id>\n", seq_parameter_set_id);

    if (44 == profile_idc ||
        83 == profile_idc ||
        86 == profile_idc ||
        100 == profile_idc ||
        110 == profile_idc ||
        118 == profile_idc ||
        122 == profile_idc ||
        128 == profile_idc ||
        134 == profile_idc ||
        135 == profile_idc ||
        138 == profile_idc ||
        139 == profile_idc ||
        244 == profile_idc)
    {
        uint8_t chroma_format_idc = UEGParse(bs);
        util::printfXml(3, "<chroma_format_idc>%d</chroma_format_idc>\n", chroma_format_idc);

        if (3 == chroma_format_idc)
        {
            uint8_t separate_colour_plane_flag = bs.GetBits(1);
            util::printfXml(4, "<separate_colour_plane_flag>%d</separate_colour_plane_flag>\n", separate_colour_plane_flag);
        }

        uint8_t bit_depth_luma_minus8 = UEGParse(bs);
        util::printfXml(3, "<bit_depth_luma_minus8>%d</bit_depth_luma_minus8>\n", bit_depth_luma_minus8);

        uint8_t bit_depth_chroma_minus8 = UEGParse(bs);
        util::printfXml(3, "<bit_depth_chroma_minus8>%d</bit_depth_chroma_minus8>\n", bit_depth_chroma_minus8);

        uint8_t qpprime_y_zero_transform_bypass_flag = bs.GetBits(1);
        util::printfXml(3, "<qpprime_y_zero_transform_bypass_flag>%d</qpprime_y_zero_transform_bypass_flag>\n", qpprime_y_zero_transform_bypass_flag);

        uint8_t seq_scaling_matrix_present_flag = bs.GetBits(1);
        util::printfXml(3, "<seq_scaling_matrix_present_flag>%d</seq_scaling_matrix_present_flag>\n", seq_scaling_matrix_present_flag);

        if (seq_scaling_matrix_present_flag)
        {
            int count = chroma_format_idc != 3 ? 8 : 12;
            uint8_t seq_scaling_list_present_flag;

            for (int i = 0; i < count; i++)
            {
                seq_scaling_list_present_flag = bs.GetBits(1);
                util::printfXml(4, "<seq_scaling_list_present_flag[%d]>%d</seq_scaling_list_present_flag>\n", i, seq_scaling_list_present_flag);

                /*
                if (seq_scaling_list_present_flag[i])
                {
                    if (i < 6)
                    {
                        scaling_list(ScalingList4x4[i], 16, UseDefaultScalingMatrix4x4Flag[i])
                    }
                    else
                    {
                        scaling_list(ScalingList8x8[i − 6], 64, UseDefaultScalingMatrix8x8Flag[i − 6])
                    }
                }
                */
            }
        }
    }

    uint8_t log2_max_frame_num_minus4 = UEGParse(bs);
    util::printfXml(3, "<log2_max_frame_num_minus4>%d</log2_max_frame_num_minus4>\n", log2_max_frame_num_minus4);

    uint8_t pic_order_cnt_type = UEGParse(bs);
    util::printfXml(3, "<pic_order_cnt_type>%d</pic_order_cnt_type>\n", pic_order_cnt_type);

    if (0 == pic_order_cnt_type)
    {
        uint8_t log2_max_pic_order_cnt_lsb_minus4 = UEGParse(bs); // 0 to 12, inclusive
        util::printfXml(3, "<log2_max_pic_order_cnt_lsb_minus4>%d</log2_max_pic_order_cnt_lsb_minus4>\n", log2_max_pic_order_cnt_lsb_minus4);
    }
    else if (1 == pic_order_cnt_type)
    {
        uint8_t delta_pic_order_always_zero_flag = bs.GetBits(1);
        util::printfXml(3, "<delta_pic_order_always_zero_flag>%d</delta_pic_order_always_zero_flag>\n", delta_pic_order_always_zero_flag);

        int32_t offset_for_non_ref_pic = SEGParse(bs); // −2^31 + 1 to 2^31 − 1, inclusive
        util::printfXml(3, "<offset_for_non_ref_pic>%d</offset_for_non_ref_pic>\n", offset_for_non_ref_pic);

        int32_t offset_for_top_to_bottom_field = SEGParse(bs); // −2^31 + 1 to 2^31 − 1, inclusive
        util::printfXml(3, "<offset_for_top_to_bottom_field>%d</offset_for_top_to_bottom_field>\n", offset_for_top_to_bottom_field);

        uint8_t num_ref_frames_in_pic_order_cnt_cycle = UEGParse(bs); // 0-255
        util::printfXml(3, "<num_ref_frames_in_pic_order_cnt_cycle>%d</num_ref_frames_in_pic_order_cnt_cycle>\n", num_ref_frames_in_pic_order_cnt_cycle);

        for (int i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++)
        {
            // TODO: Create an array
            int32_t offset_for_ref_frame = SEGParse(bs); // −2^31 + 1 to 2^31 − 1, inclusive
            util::printfXml(4, "<offset_for_ref_frame[%d]>%d</offset_for_ref_frame>\n", i, offset_for_ref_frame);
        }
    }

    uint16_t max_num_ref_frames = UEGParse(bs); // 0 to MaxDpbFrames
    util::printfXml(3, "<max_num_ref_frames>%d</max_num_ref_frames>\n", max_num_ref_frames);

    uint8_t gaps_in_frame_num_value_allowed_flag = bs.GetBits(1);
    util::printfXml(3, "<gaps_in_frame_num_value_allowed_flag>%d</gaps_in_frame_num_value_allowed_flag>\n", gaps_in_frame_num_value_allowed_flag);

    uint16_t pic_width_in_mbs_minus1 = UEGParse(bs); // 0 to MaxDpbFrames
    util::printfXml(3, "<pic_width_in_mbs_minus1>%d</pic_width_in_mbs_minus1>\n", pic_width_in_mbs_minus1);

    uint16_t pic_height_in_map_units_minus1 = UEGParse(bs);
    util::printfXml(3, "<pic_height_in_map_units_minus1>%d</pic_height_in_map_units_minus1>\n", pic_height_in_map_units_minus1);

    uint8_t frame_mbs_only_flag = bs.GetBits(1);
    util::printfXml(3, "<frame_mbs_only_flag>%d</frame_mbs_only_flag>\n", frame_mbs_only_flag);

    if (0 == frame_mbs_only_flag)
    {
        uint8_t mb_adaptive_frame_field_flag = bs.GetBits(1);
        util::printfXml(4, "<mb_adaptive_frame_field_flag>%d</mb_adaptive_frame_field_flag>\n", mb_adaptive_frame_field_flag);
    }

    uint8_t direct_8x8_inference_flag = bs.GetBits(1);
    util::printfXml(3, "<direct_8x8_inference_flag>%d</direct_8x8_inference_flag>\n", direct_8x8_inference_flag);

    uint8_t frame_cropping_flag = bs.GetBits(1);
    util::printfXml(3, "<frame_cropping_flag>%d</frame_cropping_flag>\n", frame_cropping_flag);

    if (frame_cropping_flag)
    {
        uint8_t frame_crop_left_offset = UEGParse(bs);
        util::printfXml(4, "<frame_crop_left_offset>%d</frame_crop_left_offset>\n", frame_crop_left_offset);

        uint8_t frame_crop_right_offset = UEGParse(bs);
        util::printfXml(4, "<frame_crop_right_offset>%d</frame_crop_right_offset>\n", frame_crop_right_offset);

        uint8_t frame_crop_top_offset = UEGParse(bs);
        util::printfXml(4, "<frame_crop_top_offset>%d</frame_crop_top_offset>\n", frame_crop_top_offset);

        uint8_t frame_crop_bottom_offset = UEGParse(bs);
        util::printfXml(4, "<frame_crop_bottom_offset>%d</frame_crop_bottom_offset>\n", frame_crop_bottom_offset);
    }

    uint8_t vui_parameters_present_flag = bs.GetBits(1);
    util::printfXml(3, "<vui_parameters_present_flag>%d</vui_parameters_present_flag>\n", vui_parameters_present_flag);

    if (vui_parameters_present_flag)
        processVuiParameters(bs);

    util::printfXml(2, "</SPS>\n");

    return bs.m_p - pStart;
}

/*
// 7.3.2.1.1.1 Scaling list syntax
scaling_list(scalingList, sizeOfScalingList, useDefaultScalingMatrixFlag)
{
    lastScale = 8
    nextScale = 8
    for (j = 0; j < sizeOfScalingList; j++)
    {
        if (nextScale != 0)
        {
            delta_scale 0 | 1 se(v)
            nextScale = (lastScale + delta_scale + 256) % 256
            useDefaultScalingMatrixFlag = (j = = 0 && nextScale = = 0)
        }
        
        scalingList[j] = (nextScale = = 0) ? lastScale : nextScale
        lastScale = scalingList[j]
    }
}
*/

// Annex E
size_t avcParser::processVuiParameters(BitStream& bs, VuiParameters& vuiParams)
{
    uint8_t* pStart = bs.m_p;

    vuiParams = { 0 };

    vuiParams.aspect_ratio_info_present_flag = bs.GetBits(1);

    if (vuiParams.aspect_ratio_info_present_flag)
    {
        vuiParams.aspect_ratio_idc = bs.GetBits(8);

        if (255 == vuiParams.aspect_ratio_idc)
        {
            vuiParams.sar_width = bs.GetBits(16);
            vuiParams.sar_height = bs.GetBits(16);
        }
    }

    vuiParams.overscan_info_present_flag = bs.GetBits(1);

    if (vuiParams.overscan_info_present_flag)
    {
        vuiParams.overscan_appropriate_flag = bs.GetBits(1);
    }

    vuiParams.video_signal_type_present_flag = bs.GetBits(1);

    if (vuiParams.video_signal_type_present_flag)
    {
        vuiParams.video_format = bs.GetBits(3);
        vuiParams.video_full_range_flag = bs.GetBits(1);
        vuiParams.colour_description_present_flag = bs.GetBits(1);

        if (vuiParams.colour_description_present_flag)
        {
            vuiParams.colour_primaries = bs.GetBits(8);
            vuiParams.transfer_characteristics = bs.GetBits(8);
            vuiParams.matrix_coefficients = bs.GetBits(8);
        }
    }

    vuiParams.chroma_loc_info_present_flag = bs.GetBits(1);

    if (vuiParams.chroma_loc_info_present_flag)
    {
        vuiParams.chroma_sample_loc_type_top_field = UEGParse(bs);
        vuiParams.chroma_sample_loc_type_bottom_field = UEGParse(bs);
    }

    vuiParams.timing_info_present_flag = bs.GetBits(1);

    if (vuiParams.timing_info_present_flag)
    {
        vuiParams.num_units_in_tick = bs.GetBits(32);
        vuiParams.time_scale = bs.GetBits(32);
        vuiParams.fixed_frame_rate_flag = bs.GetBits(1);
    }

    vuiParams.nal_hrd_parameters_present_flag = bs.GetBits(1);

    if (vuiParams.nal_hrd_parameters_present_flag) {
        processHrdParameters(bs, vuiParams.nal_hrd_parameters);
    }

    vuiParams.vcl_hrd_parameters_present_flag = bs.GetBits(1);

    if (vuiParams.vcl_hrd_parameters_present_flag) {
        processHrdParameters(bs, vuiParams.vcl_hrd_parameters);
    }

    if (vuiParams.nal_hrd_parameters_present_flag || vuiParams.vcl_hrd_parameters_present_flag) {
        vuiParams.low_delay_hrd_flag = bs.GetBits(1);
    }

    vuiParams.pic_struct_present_flag = bs.GetBits(1);
    vuiParams.bitstream_restriction_flag = bs.GetBits(1);

    if (vuiParams.bitstream_restriction_flag)
    {
        vuiParams.motion_vectors_over_pic_boundaries_flag = bs.GetBits(1);
        vuiParams.max_bytes_per_pic_denom = UEGParse(bs);
        vuiParams.max_bits_per_mb_denom = UEGParse(bs);
        vuiParams.log2_max_mv_length_horizontal = UEGParse(bs);
        vuiParams.log2_max_mv_length_vertical = UEGParse(bs);
        vuiParams.max_num_reorder_frames = UEGParse(bs);
        vuiParams.max_dec_frame_buffering = UEGParse(bs);
    }

    return bs.m_p - pStart;
}

// Annex E
size_t avcParser::processVuiParameters(BitStream& bs)
{
    uint8_t* pStart = bs.m_p;

    uint8_t aspect_ratio_info_present_flag = bs.GetBits(1);
    util::printfXml(3, "<aspect_ratio_info_present_flag>%d</aspect_ratio_info_present_flag>\n", aspect_ratio_info_present_flag);

    if (aspect_ratio_info_present_flag)
    {
        uint8_t aspect_ratio_idc = bs.GetBits(8);
        util::printfXml(3, "<aspect_ratio_idc>%d</aspect_ratio_idc>\n", aspect_ratio_idc);

        switch (aspect_ratio_idc)
        {
        case 1:
            util::printfXml(4, "<sample_aspect_ratio>1:1</sample_aspect_ratio>\n");
            break;
        case 2:
            util::printfXml(4, "<sample_aspect_ratio>12:11</sample_aspect_ratio>\n");
            break;
        case 3:
            util::printfXml(4, "<sample_aspect_ratio>10:11</sample_aspect_ratio>\n");
            break;
        case 4:
            util::printfXml(4, "<sample_aspect_ratio>16:11</sample_aspect_ratio>\n");
            break;
        case 5:
            util::printfXml(4, "<sample_aspect_ratio>40:33</sample_aspect_ratio>\n");
            break;
        case 6:
            util::printfXml(4, "<sample_aspect_ratio>24:11</sample_aspect_ratio>\n");
            break;
        case 7:
            util::printfXml(4, "<sample_aspect_ratio>20:11</sample_aspect_ratio>\n");
            break;
        case 8:
            util::printfXml(4, "<sample_aspect_ratio>32:11</sample_aspect_ratio>\n");
            break;
        case 9:
            util::printfXml(4, "<sample_aspect_ratio>80:33</sample_aspect_ratio>\n");
            break;
        case 10:
            util::printfXml(4, "<sample_aspect_ratio>18:11</sample_aspect_ratio>\n");
            break;
        case 11:
            util::printfXml(4, "<sample_aspect_ratio>15:11</sample_aspect_ratio>\n");
            break;
        case 12:
            util::printfXml(4, "<sample_aspect_ratio>64:33</sample_aspect_ratio>\n");
            break;
        case 13:
            util::printfXml(4, "<sample_aspect_ratio>160:99</sample_aspect_ratio>\n");
            break;
        case 14:
            util::printfXml(4, "<sample_aspect_ratio>4:3</sample_aspect_ratio>\n");
            break;
        case 15:
            util::printfXml(4, "<sample_aspect_ratio>3:2</sample_aspect_ratio>\n");
            break;
        case 16:
            util::printfXml(4, "<sample_aspect_ratio>2:1</sample_aspect_ratio>\n");
            break;
        case 255:
        {
            uint16_t sar_width, sar_height;

            sar_width = bs.GetBits(16);
            sar_height = bs.GetBits(16);

            util::printfXml(4, "<sample_aspect_ratio>%d:%d</sample_aspect_ratio>\n", sar_width, sar_height);
            break;
        }
        }
    }

    uint8_t overscan_info_present_flag = bs.GetBits(1);
    util::printfXml(3, "<overscan_info_present_flag>%d</overscan_info_present_flag>\n", overscan_info_present_flag);

    if (overscan_info_present_flag)
    {
        uint8_t overscan_appropriate_flag = bs.GetBits(1);
        util::printfXml(4, "<overscan_appropriate_flag>%d</overscan_appropriate_flag>\n", overscan_appropriate_flag);
    }

    uint8_t video_signal_type_present_flag = bs.GetBits(1);
    util::printfXml(3, "<video_signal_type_present_flag>%d</video_signal_type_present_flag>\n", video_signal_type_present_flag);

    if (video_signal_type_present_flag)
    {
        uint8_t video_format = bs.GetBits(3);

        switch (video_format)
        {
        case eAVCVideoFormatComponent:
            util::printfXml(4, "<video_format>%d: Component</video_format>\n", video_format);
            break;
        case eAVCVideoFormatPAL:
            util::printfXml(4, "<video_format>%d: PAL</video_format>\n", video_format);
            break;
        case eAVCVideoFormatNTSC:
            util::printfXml(4, "<video_format>%d: NTSC</video_format>\n", video_format);
            break;
        case eAVCVideoFormatSECAM:
            util::printfXml(4, "<video_format>%d: SECAM</video_format>\n", video_format);
            break;
        case eAVCVideoFormatMAC:
            util::printfXml(4, "<video_format>%d: MAC</video_format>\n", video_format);
            break;
        case eAVCVideoFormatUnspecified:
            util::printfXml(4, "<video_format>%d: Unspecified</video_format>\n", video_format);
            break;
        case eAVCVideoFormatReserved1:
        case eAVCVideoFormatReserved2:
            util::printfXml(4, "<video_format>%d: Reserved</video_format>\n", video_format);
            break;
        }

        uint8_t video_full_range_flag = bs.GetBits(1);
        util::printfXml(4, "<video_full_range_flag>%d</video_full_range_flag>\n", video_full_range_flag);

        uint8_t colour_description_present_flag = bs.GetBits(1);
        util::printfXml(4, "<colour_description_present_flag>%d</colour_description_present_flag>\n", colour_description_present_flag);

        if (colour_description_present_flag)
        {
            uint8_t colour_primaries = bs.GetBits(8);
            util::printfXml(5, "<colour_primaries>%d</colour_primaries>\n", colour_primaries);

            uint8_t transfer_characteristics = bs.GetBits(8);
            util::printfXml(5, "<transfer_characteristics>%d</transfer_characteristics>\n", transfer_characteristics);

            uint8_t matrix_coefficients = bs.GetBits(8);
            util::printfXml(5, "<matrix_coefficients>%d</matrix_coefficients>\n", matrix_coefficients);
        }
    }

    uint8_t chroma_loc_info_present_flag = bs.GetBits(1);
    util::printfXml(3, "<chroma_loc_info_present_flag>%d</chroma_loc_info_present_flag>\n", chroma_loc_info_present_flag);

    if (chroma_loc_info_present_flag)
    {
        uint16_t chroma_sample_loc_type_top_field = UEGParse(bs);
        util::printfXml(4, "<chroma_sample_loc_type_top_field>%d</chroma_sample_loc_type_top_field>\n", chroma_sample_loc_type_top_field);

        uint16_t chroma_sample_loc_type_bottom_field = UEGParse(bs);
        util::printfXml(4, "<chroma_sample_loc_type_bottom_field>%d</chroma_sample_loc_type_bottom_field>\n", chroma_sample_loc_type_bottom_field);
    }

    uint8_t timing_info_present_flag = bs.GetBits(1);
    util::printfXml(3, "<timing_info_present_flag>%d</timing_info_present_flag>\n", timing_info_present_flag);

    if (timing_info_present_flag)
    {
        uint32_t num_units_in_tick, time_scale;

        num_units_in_tick = bs.GetBits(32);
        util::printfXml(4, "<num_units_in_tick>%d</num_units_in_tick>\n", num_units_in_tick);

        time_scale = bs.GetBits(32);
        util::printfXml(4, "<time_scale>%d</time_scale>\n", time_scale);

        uint8_t fixed_frame_rate_flag = bs.GetBits(1);
        util::printfXml(4, "<fixed_frame_rate_flag>%d</fixed_frame_rate_flag>\n", fixed_frame_rate_flag);
    }

    uint8_t nal_hrd_parameters_present_flag = bs.GetBits(1);
    util::printfXml(3, "<nal_hrd_parameters_present_flag>%d</nal_hrd_parameters_present_flag>\n", nal_hrd_parameters_present_flag);

    if (nal_hrd_parameters_present_flag) {
        processHrdParameters(bs);
    }

    uint8_t vcl_hrd_parameters_present_flag = bs.GetBits(1);
    util::printfXml(3, "<vcl_hrd_parameters_present_flag>%d</vcl_hrd_parameters_present_flag>\n", vcl_hrd_parameters_present_flag);

    if (vcl_hrd_parameters_present_flag) {
        processHrdParameters(bs);
    }

    if (nal_hrd_parameters_present_flag || vcl_hrd_parameters_present_flag) {
        uint8_t low_delay_hrd_flag = bs.GetBits(1);
        util::printfXml(3, "<low_delay_hrd_flag>%d</low_delay_hrd_flag>\n", low_delay_hrd_flag);
    }

    uint8_t pic_struct_present_flag = bs.GetBits(1);
    util::printfXml(3, "<pic_struct_present_flag>%d</pic_struct_present_flag>\n", pic_struct_present_flag);

    uint8_t bitstream_restriction_flag = bs.GetBits(1);
    util::printfXml(3, "<bitstream_restriction_flag>%d</bitstream_restriction_flag>\n", bitstream_restriction_flag);

    if (bitstream_restriction_flag)
    {
        uint8_t motion_vectors_over_pic_boundaries_flag = bs.GetBits(1);
        util::printfXml(4, "<motion_vectors_over_pic_boundaries_flag>%d</motion_vectors_over_pic_boundaries_flag>\n", motion_vectors_over_pic_boundaries_flag);

        uint16_t max_bytes_per_pic_denom = UEGParse(bs);
        util::printfXml(4, "<max_bytes_per_pic_denom>%d</max_bytes_per_pic_denom>\n", max_bytes_per_pic_denom);

        uint16_t max_bits_per_mb_denom = UEGParse(bs);
        util::printfXml(4, "<max_bits_per_mb_denom>%d</max_bits_per_mb_denom>\n", max_bits_per_mb_denom);

        uint16_t log2_max_mv_length_horizontal = UEGParse(bs);
        util::printfXml(4, "<log2_max_mv_length_horizontal>%d</log2_max_mv_length_horizontal>\n", log2_max_mv_length_horizontal);

        uint16_t log2_max_mv_length_vertical = UEGParse(bs);
        util::printfXml(4, "<log2_max_mv_length_vertical>%d</log2_max_mv_length_vertical>\n", log2_max_mv_length_vertical);

        uint16_t max_num_reorder_frames = UEGParse(bs);
        util::printfXml(4, "<max_num_reorder_frames>%d</max_num_reorder_frames>\n", max_num_reorder_frames);

        uint16_t max_dec_frame_buffering = UEGParse(bs);
        util::printfXml(4, "<max_dec_frame_buffering>%d</max_dec_frame_buffering>\n", max_dec_frame_buffering);
    }

    return bs.m_p - pStart;
}

// E.1.2
size_t avcParser::processHrdParameters(BitStream& bs, HrdParameters& hrdParams)
{
    uint8_t* pStart = bs.m_p;
    hrdParams.cpb_cnt_minus1 = UEGParse(bs);
    hrdParams.bit_rate_scale = bs.GetBits(4);
    hrdParams.cpb_size_scale = bs.GetBits(4);

    for (int SchedSelIdx = 0; SchedSelIdx <= hrdParams.cpb_cnt_minus1; SchedSelIdx++)
    {
        uint16_t bit_rate_value_minus1, cpb_size_value_minus1;
        uint8_t cbr_flag;

        bit_rate_value_minus1 = UEGParse(bs);
        cpb_size_value_minus1 = UEGParse(bs);
        cbr_flag = bs.GetBits(1);

        hrdParams.sched_sel_idx.emplace_back(bit_rate_value_minus1, cpb_size_value_minus1, cbr_flag);
    }

    hrdParams.initial_cpb_removal_delay_length_minus1 = bs.GetBits(5);
    hrdParams.cpb_removal_delay_length_minus1 = bs.GetBits(5);
    hrdParams.dpb_output_delay_length_minus1 = bs.GetBits(5);
    hrdParams.time_offset_length = bs.GetBits(5);

    return bs.m_p - pStart;
}

// E.1.2
size_t avcParser::processHrdParameters(BitStream& bs)
{
    uint8_t* pStart = bs.m_p;

    uint8_t cpb_cnt_minus1 = UEGParse(bs);
    util::printfXml(4, "<cpb_cnt_minus1>%d</cpb_cnt_minus1>\n", cpb_cnt_minus1);

    uint8_t bit_rate_scale = bs.GetBits(4);
    util::printfXml(4, "<bit_rate_scale>%d</bit_rate_scale>\n", bit_rate_scale);

    uint8_t cpb_size_scale = bs.GetBits(4);
    util::printfXml(4, "<cpb_size_scale>%d</cpb_size_scale>\n", cpb_size_scale);

    for (int SchedSelIdx = 0; SchedSelIdx <= cpb_cnt_minus1; SchedSelIdx++)
    {
        uint16_t bit_rate_value_minus1, cpb_size_value_minus1;
        uint8_t cbr_flag;

        bit_rate_value_minus1 = UEGParse(bs);
        util::printfXml(5, "<bit_rate_value_minus1[%d]>%d</bit_rate_value_minus1>\n", SchedSelIdx, bit_rate_value_minus1);

        cpb_size_value_minus1 = UEGParse(bs);
        util::printfXml(5, "<cpb_size_value_minus1[%d]>%d</cpb_size_value_minus1>\n", SchedSelIdx, cpb_size_value_minus1);

        cbr_flag = bs.GetBits(1);
        util::printfXml(5, "<cbr_flag[%d]>%d</cbr_flag>\n", SchedSelIdx, cbr_flag);
    }

    uint8_t initial_cpb_removal_delay_length_minus1 = bs.GetBits(5);
    util::printfXml(4, "<initial_cpb_removal_delay_length_minus1>%d</initial_cpb_removal_delay_length_minus1>\n", initial_cpb_removal_delay_length_minus1);

    uint8_t cpb_removal_delay_length_minus1 = bs.GetBits(5);
    util::printfXml(4, "<cpb_removal_delay_length_minus1>%d</cpb_removal_delay_length_minus1>\n", cpb_removal_delay_length_minus1);

    uint8_t dpb_output_delay_length_minus1 = bs.GetBits(5);
    util::printfXml(4, "<dpb_output_delay_length_minus1>%d</dpb_output_delay_length_minus1>\n", dpb_output_delay_length_minus1);

    uint8_t time_offset_length = bs.GetBits(5);
    util::printfXml(4, "<time_offset_length>%d</time_offset_length>\n", time_offset_length);

    return bs.m_p - pStart;
}

/*
Table 7-6 – Name association to slice_type
--------------------------------------------
slice_type  Name of slice_type
0           P(P slice)
1           B(B slice)
2           I(I slice)
3           SP(SP slice)
4           SI(SI slice)
5           P(P slice)
6           B(B slice)
7           I(I slice)
8           SP(SP slice)
9           SI(SI slice)
*/

/*
7.4.2.4

primary_pic_type indicates that the slice_type values for all slices of the primary coded picture are members of the set listed in Table 7 - 5 for the given value of primary_pic_type.

Table 7-5 – Meaning of primary_pic_type
-----------------------------------------
primary_pic_type    slice_type values that may be present in the primary coded picture
0                   2, 7
1                   0, 2, 5, 7
2                   0, 1, 2, 5, 6, 7
3                   4, 9
4                   3, 4, 8, 9
5                   2, 4, 7, 9
6                   0, 2, 3, 4, 5, 7, 8, 9
7                   0, 1, 2, 3, 4, 5, 6, 7, 8, 9
*/

size_t avcParser::processAccessUnitDelimiter(uint8_t*& p, AccessUnitDelimiter& aud)
{
    uint8_t* pStart = p;

    aud.primary_pic_type = (*p & 0xE0) >> 5;
    util::incrementPtr(p, 1);

    return p - pStart;
}

size_t avcParser::processAccessUnitDelimiter(uint8_t*& p)
{
    uint8_t* pStart = p;

    uint8_t primary_pic_type = (*p & 0xE0) >> 5;
    util::incrementPtr(p, 1);

    return p - pStart;
}

// Exp-Golomb Parse, Clause 9.1
uint8_t avcParser::UEGParse(BitStream &bs)
{
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

// 9.1.1, Table 9-3
int avcParser::SEGParse(BitStream& bs)
{
    uint8_t codeNum = UEGParse(bs);
    return static_cast<int>(std::pow(-1, codeNum + 1) * ceil((double) codeNum / 2.f));
}
