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

#pragma once

#include <cstdint>
#include <base_parser.h>

/*
    Table 6-1

    Reserved: B0, B1, B6
*/
/*
    From: http://dvd.sourceforge.net/dvdinfo/mpeghdrs.html

    Stream ID	used for
    0xB9	    Program end (terminates a program stream)
    0xBA	    Pack header
    0xBB	    System Header
    0xBC	    Program Stream Map
    0xBD	    Private stream 1
    0xBE	    Padding stream
    0xBF	    Private stream 2
    0xC0 - 0xDF	MPEG-1 or MPEG-2 audio stream
    0xE0 - 0xEF	MPEG-1 or MPEG-2 video stream
    0xF0	    ECM Stream
    0xF1	    EMM Stream
    0xF2	    ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Annex A or ISO/IEC 13818-6_DSMCC_stream
    0xF3	    ISO/IEC_13522_stream
    0xF4	    ITU-T Rec. H.222.1 type A
    0xF5	    ITU-T Rec. H.222.1 type B
    0xF6	    ITU-T Rec. H.222.1 type C
    0xF7	    ITU-T Rec. H.222.1 type D
    0xF8	    ITU-T Rec. H.222.1 type E
    0xF9	    ancillary_stream
    0xFA - 0xFE	reserved
    0xFF	    Program Stream Directory
*/
enum eMpeg2StartCode
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
enum eMpeg2ExtensionStartCodeIdentifier
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

/*
    Taken from 6.2.2 Video Sequence

    Certain extensions are valid only at certain times.
    They all follow after the standard extension_start_code of 0x000001B5.
    The TYPE of data that follows the extension_strt_code is context dependent.
*/
enum eMpeg2ExtensionType
{
    sequence_extension = 0,
    picture_coding_extension,
    extension_and_user_data_0,
    extension_and_user_data_1,
    extension_and_user_data_2,
    extension_unknown
};

/*
    Table 6-4 --- frame_rate_value
*/
enum eMpeg2FrameRateValue
{
    frame_rate_forbidden = 0,   // forbidden
    frame_rate_23976 = 1,       // 24 000�1001 (23,976)
    frame_rate_24 = 2,          // 24
    frame_rate_25 = 3,          // 25
    frame_rate_2997 = 4,        // 30 000�1001 (29,97)
    frame_rate_30 = 5,          // 30
    frame_rate_50 = 6,          // 50
    frame_rate_5994 = 7,        // 60 000�1001 (59,94)
    frame_rate_60 = 8           // 60
    // . . ., reserved
    // 0xFF, reserved
};

class mpeg2Parser : public baseParser
{
public:

    mpeg2Parser()
        : m_nextMpeg2ExtensionType(sequence_extension)
        , m_frameNumber(0)
    {}

    // Process framesWanted frames at a time
    virtual size_t processVideoFrames(uint8_t* p,
        size_t PESPacketDataLength,
        unsigned int& frameNumber, // Will be incremented by 1 per parsed frame
        unsigned int framesWanted,
        unsigned int& framesReceived) override;

private:
    // Entire stream data available in memory
    size_t processVideoPES(uint8_t *p, size_t PESPacketDataLength);
    size_t processSequenceHeader(uint8_t *&p);
    size_t processSequenceExtension(uint8_t *&p);
    size_t processSequenceDisplayExtension(uint8_t *&p);
    size_t processSequenceScalableExtension(uint8_t *&p);
    size_t processExtensionAndUserData0(uint8_t *&p);
    size_t processExtension(uint8_t *&p);
    size_t processGroupOfPicturesHeader(uint8_t *&p);
    size_t processPictureHeader(uint8_t *&p);
    size_t processPictureCodingExtension(uint8_t *&p);
    size_t processUserData(uint8_t *&p);
    size_t processSlice(uint8_t *&p);

    eMpeg2ExtensionType m_nextMpeg2ExtensionType;
    unsigned int m_frameNumber = 0;
};