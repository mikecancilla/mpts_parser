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
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cassert>

#include "mpts_parser.h"

static bool g_b_debug = false;
static bool g_b_progress = false;
static bool g_b_xml = true;
static bool g_b_terse = true;
static bool g_b_analyze_elementary_stream = false;

size_t g_file_position = 0;

uint8_t g_test_packet[188] = { 0x47, 0x00, 0x31, 0x35, 0x57, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x46, 0xCD, 0x90, 0xE6, 0xF1, 0x0D, 0x1A, 0xB5, 0xA6, 0x36, 0xFA, 0x5E, 0x17, 0x23, 0x75, 0x8F, 0x6F, 0x8F, 0x34, 0x68, 0xD6, 0xA8, 0xDB, 0xEA, 0x34, 0x3A, 0xB0, 0x39, 0xBE, 0x5E, 0xD1, 0xA3, 0x51, 0xAB, 0x1B, 0x7B, 0xFA, 0x53, 0x55, 0x16, 0xA3, 0x78, 0x56, 0x8D, 0x7A, 0xCA, 0x36, 0xF5, 0x84, 0xC4, 0x6E, 0x92, 0x5D, 0x6F, 0x02, 0xD1, 0xB4, 0xAD, 0x11, 0xB7, 0xD7, 0x61, 0x6D, 0xCA, 0xD0, 0xE8, 0xDF, 0x37, 0x68, 0xD9, 0x6B, 0x54, 0x6D, 0xEA, 0x9A, 0x96, 0xF3, 0x6D, 0x1B, 0x6A, 0xD1, 0x1B, 0x7A, 0x2A, 0xCE, 0xDE, 0x69, 0xA3, 0x55, 0x62, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00 };

static void inline my_printf(const char *format, ...)
{
    if(g_b_debug)
    {
        va_list arg_list;
        va_start(arg_list, format);
        vprintf(format, arg_list);
    }
}

static void inline printf_xml(unsigned int indent_level, const char *format, ...)
{
    if(g_b_xml && format)
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

// It all starts here
int main(int argc, char* argv[])
{
//    tinyxml2::XMLDocument* doc = new tinyxml2::XMLDocument();

    if (0 == argc)
    {
        fprintf(stderr, "%s: Output extensive xml representation of MP2TS file to stdout\n", argv[0]);
        fprintf(stderr, "Usage: %s [-g] [-p] [-q] mp2ts_file\n", argv[0]);
        fprintf(stderr, "-e: Also analyze the video elementary stream in the MP2TS\n");
        fprintf(stderr, "-p: Print progress on a single line to stderr\n");
        fprintf(stderr, "-q: No output. Run through the file and only print errors\n");
        fprintf(stderr, "-v: Verbose output. Careful with this one\n");
        return 0;
    }

    for (int i = 1; i < argc - 1; i++)
    {
        if (0 == strcmp("-d", argv[i]))
            g_b_debug = true;

        if (0 == strcmp("-p", argv[i]))
            g_b_progress = true;

        if(0 == strcmp("-q", argv[i]))
            g_b_xml = false;

        if(0 == strcmp("-v", argv[i]))
            g_b_terse = false;

        if(0 == strcmp("-e", argv[i]))
            g_b_analyze_elementary_stream = true;
    }

    mpts_parser mpts(g_file_position);
    mpts.set_print_xml(g_b_xml);
    mpts.set_terse(g_b_terse);
    mpts.set_analyze_elementary_stream(g_b_analyze_elementary_stream);

    uint8_t *packet_buffer, *packet;
	uint16_t program_map_pid = 0;
    unsigned int packet_num = 0;

	size_t packet_buffer_size = 0;
    int64_t total_read = 0;
    int64_t read_block_size = 0;

	FILE *f = nullptr;
	fopen_s(&f, argv[argc - 1], "rb");

	if (nullptr == f)
    {
        fprintf(stderr, "%s: Can't open input file", argv[0]);
		return -1;
    }

    // Determine the size of the file
    int64_t file_size = 0;

#ifdef WINDOWS
    struct __stat64 stat64_buf;
    _stat64(argv[argc - 1], &stat64_buf);
    file_size = stat64_buf.st_size;
#else
    fseek(f, 0L, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0L, SEEK_SET);
#endif

    // Need to determine packet size.
    // Standard is 188, but digital video cameras add a 4 byte timecode
    // before the 188 byte packet, making the packet size 192.
    // https://en.wikipedia.org/wiki/MPEG_transport_stream

    uint8_t temp_buffer[5];
    fread(temp_buffer, 1, 5, f);

    int packet_size = mpts.determine_packet_size(temp_buffer);

    if(-1 == packet_size)
    {
        fprintf(stderr, "%s: Can't recognize the input file", argv[0]);
        return -1;
    }

    // Go back to the beginning of the file
    fseek(f, 0L, SEEK_SET);

    if(file_size > 10000*packet_size)
        read_block_size = 10000*packet_size;
    else
        read_block_size = file_size;

    packet_buffer = new uint8_t[read_block_size];

    // Read each 188 byte packet and process the packet
	packet_buffer_size = fread(packet_buffer, 1, read_block_size, f);
    packet = packet_buffer;

    printf_xml(0, "<?xml version = \"1.0\" encoding = \"UTF-8\"?>\n");
    printf_xml(0, "<file>\n");
    printf_xml(1, "<name>%s</name>\n", argv[argc - 1]);
    printf_xml(1, "<file_size>%llu</file_size>\n", file_size);
    printf_xml(1, "<packet_size>%d</packet_size>\n", packet_size);
    if(g_b_terse)
        printf_xml(1, "<terse>1</terse>\n");
    else
        printf_xml(1, "<terse>0</terse>\n");

    float step = 1.f;
    float nextStep = 0.f;
    float progress = 0.f;

    // Send one packet at a time into the mpts_parser
	while((size_t) (packet - packet_buffer) < packet_buffer_size)
	{
        int err = 0;

        if(192 == packet_size)
            err = mpts.process_packet(packet + 4, packet_num);
        else
            err = mpts.process_packet(packet, packet_num);

        if(0 != err)
            goto error;

        total_read += packet_size;
        g_file_position = total_read;

        if(g_b_progress)
        {
            if(progress >= nextStep)
            {
                fprintf(stderr, "Total bytes processed: %llu, %2.2f%%\r", total_read, progress);
                nextStep += step;
            }

            progress = ((float)total_read / (float)file_size) * 100.f;
        }

        if(0 == (total_read % read_block_size))
        {
            packet_buffer_size = fread(packet_buffer, 1, read_block_size, f);
            packet = packet_buffer;
        }
        else
            packet += packet_size;

        assert(packet_buffer_size > 0);

        packet_num++;
    }

    mpts.flush();

error:
    printf_xml(0, "</file>\n");

    delete packet_buffer;

	fclose(f);

	return 0;
}
