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

/*
* TODO:
*/

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cassert>
#include "mpts_parser.h"
#include "util.h"

uint8_t g_test_packet[188] = { 0x47, 0x00, 0x31, 0x35, 0x57, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x46, 0xCD, 0x90, 0xE6, 0xF1, 0x0D, 0x1A, 0xB5, 0xA6, 0x36, 0xFA, 0x5E, 0x17, 0x23, 0x75, 0x8F, 0x6F, 0x8F, 0x34, 0x68, 0xD6, 0xA8, 0xDB, 0xEA, 0x34, 0x3A, 0xB0, 0x39, 0xBE, 0x5E, 0xD1, 0xA3, 0x51, 0xAB, 0x1B, 0x7B, 0xFA, 0x53, 0x55, 0x16, 0xA3, 0x78, 0x56, 0x8D, 0x7A, 0xCA, 0x36, 0xF5, 0x84, 0xC4, 0x6E, 0x92, 0x5D, 0x6F, 0x02, 0xD1, 0xB4, 0xAD, 0x11, 0xB7, 0xD7, 0x61, 0x6D, 0xCA, 0xD0, 0xE8, 0xDF, 0x37, 0x68, 0xD9, 0x6B, 0x54, 0x6D, 0xEA, 0x9A, 0x96, 0xF3, 0x6D, 0x1B, 0x6A, 0xD1, 0x1B, 0x7A, 0x2A, 0xCE, 0xDE, 0x69, 0xA3, 0x55, 0x62, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00 };

// It all starts here
int main(int argc, char* argv[])
{
//    tinyxml2::XMLDocument* doc = new tinyxml2::XMLDocument();

    bool xmlOut = true;
    bool bProgress = false;
    bool bTerse = true;
    bool bAnalyzeElementaryStream = false;
    size_t filePosition = 0;

    if (1 == argc)
    {
        fprintf(stderr, "%s: Output extensive xml representation of MPTS file to stdout\n", argv[0]);
        fprintf(stderr, "Usage: %s [-e] [-p] [-q] [-v] mpts_file\n", argv[0]);
        fprintf(stderr, "-e: Also analyze the video elementary stream in the MPTS\n");
        fprintf(stderr, "-p: Print progress on a single line to stderr\n");
        fprintf(stderr, "-q: No output. Run through the file and only print errors\n");
        fprintf(stderr, "-v: Verbose output. Careful with this one\n");
        return 0;
    }

    for (int i = 1; i < argc - 1; i++)
    {
        if (0 == strcmp("-p", argv[i]))
            bProgress = true;

        if(0 == strcmp("-q", argv[i]))
            xmlOut = false;

        if(0 == strcmp("-v", argv[i]))
            bTerse = false;

        if(0 == strcmp("-e", argv[i]))
            bAnalyzeElementaryStream = true;
    }

    util::setXmlOutput(xmlOut);

    mptsParser mpts(filePosition);
    mpts.setTerse(bTerse);
    mpts.setAnalyzeElementaryStream(bAnalyzeElementaryStream);

    uint8_t *packetBuffer, *packet;
	uint16_t programMapPid = 0;
    unsigned int packetNum = 0;

	size_t packetBufferSize = 0;
    int64_t totalRead = 0;
    int64_t readBlockSize = 0;

	FILE *inputFile = nullptr;
	fopen_s(&inputFile, argv[argc - 1], "rb");

	if (nullptr == inputFile)
    {
        fprintf(stderr, "%s: Can't open input file", argv[0]);
		return -1;
    }

    // Determine the size of the file
    int64_t fileSize = 0;

#ifdef WINDOWS
    struct __stat64 stat64Buf;
    _stat64(argv[argc - 1], &stat64Buf);
    fileSize = stat64Buf.st_size;
#else
    fseek(f, 0L, SEEK_END);
    fileSize = ftell(f);
    fseek(f, 0L, SEEK_SET);
#endif

    // Need to determine packet size.
    // Standard is 188, but digital video cameras add a 4 byte timecode
    // before the 188 byte packet, making the packet size 192.
    // https://en.wikipedia.org/wiki/MPEG_transport_stream

    uint8_t tempBuffer[5];
    fread(tempBuffer, 1, 5, inputFile);

    int packetSize = mpts.determine_packet_size(tempBuffer);

    if(-1 == packetSize)
    {
        fprintf(stderr, "%s: Can't recognize the input file", argv[0]);
        return -1;
    }

    // Go back to the beginning of the file
    fseek(inputFile, 0L, SEEK_SET);

    if(fileSize > 10000*(int64_t)packetSize)
        readBlockSize = 10000*(int64_t)packetSize;
    else
        readBlockSize = fileSize;

    packetBuffer = new uint8_t[readBlockSize];

    // Read each 188 byte packet and process the packet
	packetBufferSize = fread(packetBuffer, 1, readBlockSize, inputFile);
    packet = packetBuffer;

    util::printfXml(0, "<?xml version = \"1.0\" encoding = \"UTF-8\"?>\n");
    util::printfXml(0, "<file>\n");
    util::printfXml(1, "<name>%s</name>\n", argv[argc - 1]);
    util::printfXml(1, "<file_size>%llu</file_size>\n", fileSize);
    util::printfXml(1, "<packet_size>%d</packet_size>\n", packetSize);
    if(bTerse)
        util::printfXml(1, "<terse>1</terse>\n");
    else
        util::printfXml(1, "<terse>0</terse>\n");

    float step = 1.f;
    float nextStep = 0.f;
    float progress = 0.f;

    // Send one packet at a time into the mpts_parser
	while((size_t) (packet - packetBuffer) < packetBufferSize)
	{
        int err = 0;

//        if(packetNum == 88892)
//            __debugbreak();

        if(192 == packetSize)
            err = mpts.processPacket(packet + 4, packetNum);
        else
            err = mpts.processPacket(packet, packetNum);

        if(0 != err)
            goto error;

        totalRead += packetSize;
        filePosition = totalRead;

        if(bProgress)
        {
            if(progress >= nextStep)
            {
                fprintf(stderr, "Total bytes processed: %llu, %2.2f%%\r", totalRead, progress);
                nextStep += step;
            }

            progress = ((float)totalRead / (float)fileSize) * 100.f;
        }

        if(0 == (totalRead % readBlockSize))
        {
            packetBufferSize = fread(packetBuffer, 1, readBlockSize, inputFile);
            packet = packetBuffer;
        }
        else
            packet += packetSize;

        packetNum++;
    }

    mpts.flush();

error:
    util::printfXml(0, "</file>\n");

    delete [] packetBuffer;

	fclose(inputFile);

	return 0;
}
