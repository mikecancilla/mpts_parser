#pragma once

#include <cstdint>

class base_parser
{
protected:
    bool m_b_xml_out;

public:
    base_parser()
      : m_b_xml_out(false)
    {}

    // Process the number of video frames_wanted pointed to by p, of length data_length.
    // Print xml if b_xml_out is true
    // Return number of frames actually processed in frames_received
    virtual size_t process_video_frames(uint8_t *p, size_t data_length, unsigned int frames_wanted, unsigned int &frames_received, bool b_xml_out) = 0;
};