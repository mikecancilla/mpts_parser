#pragma once

#include <cstdint>
#include <any>

class baseParser
{
public:
    baseParser() {}

    virtual size_t processVideoFrame(uint8_t* p,
        size_t dataLength,
        std::any& returnData)
    {
        return 0;
    }

    // Process the number of video frames_wanted pointed to by p, of length dataLength.
    // Return number of frames actually processed in frames_received
    virtual size_t processVideoFrames(uint8_t* p,
        size_t dataLength,
        unsigned int& frameNumber, // Will be incremented by 1 per parsed frame
        unsigned int framesWanted,
        unsigned int& framesReceived)
    {
        return 0;
    }
};