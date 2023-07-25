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

#include <cstdio>
#include <cassert>
#include <cstring> // memcpy
#include "mpeg2_parser.h"
#include "util.h"

size_t mpeg2Parser::processVideoFrames(uint8_t *p, size_t PESPacketDataLength, unsigned int& frameNumber, unsigned int framesWanted, unsigned int &framesReceived)
{
    uint8_t *pStart = p;
    size_t bytesProcessed = 0;
    bool bDone = false;
    framesReceived = 0;

    while(bytesProcessed < PESPacketDataLength && !bDone)
    {
RETRY:
        uint32_t startCode = util::read4Bytes(p);
        uint32_t startCodePrefix = (startCode & 0xFFFFFF00) >> 8;

        if(0x000001 != startCodePrefix)
        {
            fprintf(stderr, "WARNING: Bad data found %lu bytes into this frame.  Searching for next start code...\n", bytesProcessed);
            size_t count = util::nextStartCode(p, PESPacketDataLength);

            if(-1 == count)
            {
                bDone = true;
                continue;
            }

            goto RETRY;
        }

        startCode &= 0x000000FF;

        switch(startCode)
        {
            case picture_start_code:
                bytesProcessed += processPictureHeader(p);
                framesReceived++;

                if(framesReceived == framesWanted)
                {
                    bDone = true;
                }
            break;

            case user_data_start_code:
                //bytesProcessed += process_user_data(p);
                bytesProcessed += util::skipToNextStartCode(p);
            break;

            case sequence_header_code:
                //bytesProcessed += process_sequence_header(p);
                bytesProcessed += util::skipToNextStartCode(p);
            break;

            case sequence_error_code:
                bDone = true;
            break;

            case extension_start_code:
                //bytesProcessed += process_extension(p);
                bytesProcessed += util::skipToNextStartCode(p);
            break;

            case sequence_end_code:
                bDone = true;
            break;

            case group_start_code:
                bytesProcessed += processGroupOfPicturesHeader(p);
                //bytesProcessed += skipToNextStartCode(p);
            break;

            default:
            {
                if(startCode >= slice_start_codes_begin &&
                   startCode <= slice_start_codes_end)
                {
                    //bytesProcessed += process_slice(p);
                    bytesProcessed += util::skipToNextStartCode(p);
                }
                else
                {
                    // MPEG2 does not know what this start code is, pass control on back up
                    bDone = true;
                }
            }
        }
    }

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2
size_t mpeg2Parser::processVideoPES(uint8_t *p, size_t PESPacketDataLength)
{
    uint8_t *pStart = p;
    size_t bytesProcessed = 0;
    bool bDone = false;
    unsigned int framesReceived = 0;

    while(bytesProcessed < PESPacketDataLength && !bDone)
    {
        bytesProcessed += processVideoFrames(p, PESPacketDataLength, m_frameNumber, 1, framesReceived);
    }

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.1
size_t mpeg2Parser::processSequenceHeader(uint8_t *&p)
{
    uint8_t *pStart = p;

    util::validateStartCode(p, sequence_header_code);

    uint32_t fourBytes = util::read4Bytes(p);
    util::incrementPtr(p, 4);

    uint32_t horizontal_size_value = (fourBytes & 0xFFF00000) >> 20;
    uint32_t vertical_size_value = (fourBytes & 0x000FFF00) >> 8;
    uint8_t aspect_ratio_information = (fourBytes & 0xF0) >> 4;
    uint8_t frame_rate_code = fourBytes & 0x0F;

    //printf_xml(2, "<width>%d</width>\n", horizontal_size_value);
    //printf_xml(2, "<height>%d</height>\n", vertical_size_value);

    fourBytes = util::read4Bytes(p);
    util::incrementPtr(p, 4);

    // At this point p is one bit in to the intra_quantizer_matrix

    uint32_t bit_rate_value = (fourBytes & 0xFFFFC000) >> 14;
    uint16_t vbv_buffer_size_value = (fourBytes & 0x1FF8) >> 3;
    uint8_t constrained_parameters_flag = (fourBytes & 0x4) >> 2;
    uint8_t load_intra_quantizer_matrix = (fourBytes & 0x2) >> 1;

    uint8_t load_non_intra_quantizer_matrix = 0;

    if(load_intra_quantizer_matrix)
    {
        util::incrementPtr(p, 63);
        load_non_intra_quantizer_matrix = *p;
        util::incrementPtr(p, 1);
        load_non_intra_quantizer_matrix &= 0x1;
    }
    else
        load_non_intra_quantizer_matrix = fourBytes & 0x1;

    if(load_non_intra_quantizer_matrix)
        util::incrementPtr(p, 64);

    m_nextMpeg2ExtensionType = sequence_extension;

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.3
size_t mpeg2Parser::processSequenceExtension(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint32_t fourBytes = util::read4Bytes(p);
    util::incrementPtr(p, 4);

    eMpeg2ExtensionStartCodeIdentifier extension_start_code_identifier = (eMpeg2ExtensionStartCodeIdentifier) ((fourBytes & 0xF0000000) >> 28);
    assert(sequence_extension_id == extension_start_code_identifier);

    uint8_t profile_and_level_indication = (fourBytes & 0x0FF00000) >> 20;
    uint8_t progressive_sequence = (fourBytes & 0x00080000) >> 19;
    uint8_t chroma_format = (fourBytes & 0x00060000) >> 17;
    uint8_t horizontal_size_extension = (fourBytes & 0x00018000) >> 15;
    uint8_t vertical_size_extension = (fourBytes & 0x00006000) >> 13;
    uint16_t bit_rate_extension = (fourBytes & 0x00001FFE) >> 1;
    // marker_bit

    uint8_t vbv_buffer_size_extension = *p;
    util::incrementPtr(p, 1);

    uint8_t byte = *p;
    util::incrementPtr(p, 1);

    uint8_t low_delay = (byte & 0x80) >> 7;
    uint8_t frame_rate_extension_n = (byte & 0x60) >> 5;
    uint8_t frame_rate_extension_d = byte & 0x1F;

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.4
size_t mpeg2Parser::processSequenceDisplayExtension(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint8_t byte = *p;
    util::incrementPtr(p, 1);

    uint8_t video_format = (byte & 0x0E) >> 1;
    uint8_t colour_description = byte & 0x01;

    if(colour_description)
    {
        uint8_t colour_primaries = *p;
        util::incrementPtr(p, 1);

        uint8_t transfer_characteristics = *p;
        util::incrementPtr(p, 1);

        uint8_t matrix_coefficients = *p;
        util::incrementPtr(p, 1);
    }

    uint32_t fourBytes = util::read4Bytes(p);
    util::incrementPtr(p, 4);

    uint16_t display_horizontal_size = (fourBytes & 0xFFFC0000) >> 18;
    // marker_bit
    uint16_t display_vertical_size =   (fourBytes & 0x0001FFF8) >> 3;

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.5
size_t mpeg2Parser::processSequenceScalableExtension(uint8_t *&p)
{
    uint8_t *pStart = p;

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.2.1
size_t mpeg2Parser::processExtensionAndUserData0(uint8_t *&p)
{
    uint8_t *pStart = p;

    if(sequence_display_extension_id == ((*p & 0xF0) >> 4))
        processSequenceDisplayExtension(p);

    if(sequence_scalable_extension_id == ((*p & 0xF0) >> 4))
        processSequenceScalableExtension(p);

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.2.1
//
// The setting of g_next_mpeg2_extension_type follows the diagram of 6.2.2 Video Sequence
size_t mpeg2Parser::processExtension(uint8_t *&p)
{
    uint8_t *pStart = p;

    util::validateStartCode(p, extension_start_code);

    switch(m_nextMpeg2ExtensionType)
    {
        case sequence_extension:
            processSequenceExtension(p);
            m_nextMpeg2ExtensionType = extension_and_user_data_0;
        break;
        
        case picture_coding_extension:
            processPictureCodingExtension(p);
            m_nextMpeg2ExtensionType = extension_and_user_data_2;
        break;

        case extension_and_user_data_0:
            processExtensionAndUserData0(p);

            //    The next extension can be either:
            //        extension_and_user_data_1 (Follows a GOP)
            //        extension_and_user_data_2 (Follows a picture_coding_extension)
            m_nextMpeg2ExtensionType = extension_unknown;
        break;

        case extension_and_user_data_1:
        break;

        case extension_and_user_data_2:
        break;

        default:
        break;
    }

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.6
size_t mpeg2Parser::processGroupOfPicturesHeader(uint8_t *&p)
{
    uint8_t *pStart = p;

    util::validateStartCode(p, group_start_code);

    uint32_t fourBytes = util::read4Bytes(p);
    util::incrementPtr(p, 4);

    uint32_t time_code =  (fourBytes & 0xFFFFFF80) >> 7;
    uint8_t closed_gop =  (fourBytes & 0x00000040) >> 6;
    uint8_t broken_link = (fourBytes & 0x00000020) >> 5;

    util::printfXml(2, "<closed_gop>%d</closed_gop>\n", closed_gop);

    m_nextMpeg2ExtensionType = extension_and_user_data_1;

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.3
size_t mpeg2Parser::processPictureHeader(uint8_t *&p)
{
    uint8_t *pStart = p;

    util::validateStartCode(p, picture_start_code);

    uint32_t fourBytes = util::read4Bytes(p);
    util::incrementPtr(p, 4);
    
    uint16_t temporal_reference = (fourBytes & 0xFFC00000) >> 22;
    uint8_t picture_coding_type = (fourBytes & 0x00380000) >> 19;
    uint16_t vbv_delay =          (fourBytes & 0x0007FFF8) >> 3;

    uint8_t carry_over = fourBytes & 0x07;
    uint8_t carry_over_bits = 3;
    uint8_t full_pel_forward_vector = 0;
    uint8_t forward_f_code = 0;
    uint8_t full_pel_backward_vector = 0;
    uint8_t backward_f_code = 0;

    util::printfXml(2, "<type>%c</type>\n", " IPB"[picture_coding_type]);

    if(2 == picture_coding_type)
    {
        full_pel_forward_vector = (fourBytes & 0x04) >> 2;
        forward_f_code = (fourBytes & 0x03) << 2;

        carry_over = *p;
        util::incrementPtr(p, 1);

        forward_f_code |= (carry_over & 0x80) >> 7;

        carry_over &= 0x7F;
        carry_over_bits = 7;
    }
    else if(3 == picture_coding_type)
    {
        full_pel_forward_vector = (fourBytes & 0x04) >> 2;
        forward_f_code = (fourBytes & 0x03) << 2;

        carry_over = *p;
        util::incrementPtr(p, 1);

        forward_f_code |= (carry_over & 0x80) >> 7;

        full_pel_backward_vector = (carry_over & 0x40) >> 6;
        backward_f_code = (carry_over & 0x38) >> 3;

        carry_over &= 0x03;
        carry_over_bits = 2;
    }

    if(carry_over & (0x01 << (carry_over_bits-1)))
        assert(1); // TODO: Handle this case

    m_nextMpeg2ExtensionType = picture_coding_extension;

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.3.1
size_t mpeg2Parser::processPictureCodingExtension(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint32_t fourBytes = util::read4Bytes(p);
    util::incrementPtr(p, 4);

    eMpeg2ExtensionStartCodeIdentifier extension_start_code_identifier = (eMpeg2ExtensionStartCodeIdentifier) ((fourBytes & 0xF0000000) >> 28);
    assert(picture_coding_extension_id == extension_start_code_identifier);

    uint8_t f_code[4];
    f_code[0] = (fourBytes & 0x0F000000);
    f_code[1] = (fourBytes & 0x00F00000);
    f_code[2] = (fourBytes & 0x000F0000);
    f_code[3] = (fourBytes & 0x0000F000);

    uint8_t intra_dc_precision         = (fourBytes & 0x00000C00) >> 10;
    uint8_t picture_structure          = (fourBytes & 0x00000300) >> 8;
    uint8_t top_field_first            = (fourBytes & 0x00000080) >> 7;
    uint8_t frame_pred_frame_dct       = (fourBytes & 0x00000040) >> 6;
    uint8_t concealment_motion_vectors = (fourBytes & 0x00000020) >> 5;
    uint8_t q_scale_type               = (fourBytes & 0x00000010) >> 4;
    uint8_t intra_vlc_format           = (fourBytes & 0x00000008) >> 3;
    uint8_t alternate_scan             = (fourBytes & 0x00000004) >> 2;
    uint8_t repeat_first_field         = (fourBytes & 0x00000002) >> 1;
    uint8_t chroma_420_type            = fourBytes & 0x00000001;

    uint32_t byte = *p;
    util::incrementPtr(p, 1);

    uint8_t progressive_frame      = (byte & 0x80) >> 7;
    uint8_t composite_display_flag = (byte & 0x40) >> 6;

    if(composite_display_flag)
    {
        uint8_t v_axis             = (byte & 0x20) >> 5;
        uint8_t field_sequence     = (byte & 0x1C) >> 2;
        uint8_t sub_carrier        = (byte & 0x02) >> 1;
        uint8_t burst_amplitude    = (byte & 0x01) << 6;

        byte = *p;
        util::incrementPtr(p, 1);

        burst_amplitude |= (byte & 0xFC) >> 2;

        uint8_t sub_carrier_phase = (byte & 0x03) << 6;

        byte = *p;
        util::incrementPtr(p, 1);

        sub_carrier_phase |= (byte & 0xFC) >>2;
    }

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.2.2.2
size_t mpeg2Parser::processUserData(uint8_t *&p)
{
    uint8_t *pStart = p;

    util::validateStartCode(p, user_data_start_code);

    util::nextStartCode(p);

    return p - pStart;
}

// MPEG2 spec, 13818-2, 6.2.4
size_t mpeg2Parser::processSlice(uint8_t *&p)
{
    uint8_t *pStart = p;

    uint32_t fourBytes = util::read4Bytes(p);
    util::incrementPtr(p, 4);

    uint32_t start_code_prefix = (fourBytes & 0xFFFFFF00) >> 8;
    assert(0x000001 == start_code_prefix);

    uint8_t slice_number = fourBytes & 0xff;

    util::nextStartCode(p);

    return p - pStart;
}
