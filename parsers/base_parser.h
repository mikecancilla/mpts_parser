#pragma once

#include <cstdint>

class baseParser
{
protected:
    bool m_bXmlOut;

public:
    baseParser()
      : m_bXmlOut(false)
    {}

    // Process the number of video frames_wanted pointed to by p, of length data_length.
    // Print xml if m_bXmlOut is true
    // Return number of frames actually processed in frames_received
    virtual size_t processVideoFrames(uint8_t *p, size_t dataLength, unsigned int framesWanted, unsigned int &framesReceived, bool bXmlOut) = 0;
};