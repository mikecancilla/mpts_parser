#pragma once

#include <vector>
#include <variant>
#include <memory>

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

struct video_stream_descriptor
{
    uint8_t multiple_frame_rate_flag;
    uint8_t frame_rate_code;
    uint8_t mpeg_1_only_flag;
    uint8_t constrained_parameter_flag;
    uint8_t still_picture_flag;
    uint8_t profile_and_level_indication;
    uint8_t chroma_format;
    uint8_t frame_rate_extension_flag;
};

struct audio_stream_descriptor
{
    uint8_t free_format_flag;
    uint8_t id;
    uint8_t layer;
    uint8_t variable_rate_audio_indicator;
};

struct registration_descriptor
{
    uint32_t format_identifier;
    std::vector<uint8_t> additional_identification_info;
};

typedef std::variant<std::shared_ptr<video_stream_descriptor>,
                     std::shared_ptr<audio_stream_descriptor>,
                     std::shared_ptr<registration_descriptor>> mpts_descriptor;
