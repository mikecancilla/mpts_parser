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
    SCTE 35 in MPTS
    http://www.scte.org/SCTEDocs/Standards/SCTE%2035%202016.pdf
*/

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstdarg>
#include <vector>
#include <map>
#include <variant>
#include <memory>

#include "mpts_parser.h"
#include "mpts_descriptors.h"
#include "mpeg2_parser.h"
#include "avc_parser.h"

#define VIDEO_DATA_MEMORY_INCREMENT (500 * 1024)
#define SYNC_BYTE 0x47

//#define 36 - 63 n / a n / a ITU - T Rec.H.222.0 | ISO / IEC 13818 - 1 Reserved
//#define 64 - 255 n / a n / a User Private

mptsParser::mptsParser(size_t &filePosition)
    : m_pVideoData(NULL)
    , m_filePosition(filePosition)
    , m_packetSize(0)
    , m_programNumber(-1)
    , m_programMapPid(-1)
    , m_networkPid(0x0010)
    , m_scte35Pid(-1)
    , m_videoDataSize(0)
    , m_videoBufferSize(0)
    , m_bTerse(true)
    , m_bAnalyzeElementaryStream(false)
    , m_parser(nullptr)
{
}

mptsParser::~mptsParser()
{
    popVideoData();
}

bool mptsParser::setTerse(bool tf)
{
    bool ret = m_bTerse;
    m_bTerse = tf;
    return ret;
}

bool mptsParser::getTerse()
{
    return m_bTerse;
}

bool mptsParser::setAnalyzeElementaryStream(bool tf)
{
    bool ret = m_bAnalyzeElementaryStream;
    m_bAnalyzeElementaryStream = tf;
    return ret;
}

bool mptsParser::getAnalyzeElementaryStream()
{
    return m_bAnalyzeElementaryStream;
}

void inline mptsParser::incPtr(uint8_t *&p, size_t bytes)
{
    m_filePosition += util::incrementPtr(p, bytes);
}

// Table 2-34
// Initialize a map of ID to string type
void mptsParser::initStreamTypes(std::map <uint16_t, char *> &streamMap)
{
    streamMap[0x0] = "Reserved";	                            
    streamMap[0x1] = "MPEG-1 Video";
    streamMap[0x2] = "MPEG-2 Video";
    streamMap[0x3] = "MPEG-1 Audio";
    streamMap[0x4] = "MPEG-2 Audio";
    streamMap[0x5] = "ISO 13818-1 private sections";
    streamMap[0x6] = "ISO 13818-1 PES private data";
    streamMap[0x7] = "ISO 13522 MHEG";
    streamMap[0x8] = "ISO 13818-1 DSM - CC";
    streamMap[0x9] = "ISO 13818-1 auxiliary";
    streamMap[0xa] = "ISO 13818-6 multi-protocol encap";
    streamMap[0xb] = "ISO 13818-6 DSM-CC U-N msgs";
    streamMap[0xc] = "ISO 13818-6 stream descriptors";
    streamMap[0xd] = "ISO 13818-6 sections";
    streamMap[0xe] = "ISO 13818-1 auxiliary";
    streamMap[0xf] = "MPEG-2 AAC Audio";
    streamMap[0x10] = "MPEG-4 Video";
    streamMap[0x11] = "MPEG-4 LATM AAC Audio";
    streamMap[0x12] = "MPEG-4 generic";
    streamMap[0x13] = "ISO 14496-1 SL-packetized";
    streamMap[0x14] = "ISO 13818-6 Synchronized Download Protocol";
    streamMap[0x15] = "Metadata carried in PES packets";
    streamMap[0x16] = "Metadata carried in metadata_sections";
    streamMap[0x17] = "Metadata carried in ISO/IEC 13818-6 Data Carousel";
    streamMap[0x18] = "Metadata carried in ISO/IEC 13818-6 Object Carousel";
    streamMap[0x19] = "Metadata carried in ISO/IEC 13818-6 Synchronized Download Protocol";
    streamMap[0x1a] = "IPMP stream (defined in ISO/IEC 13818-11, MPEG-2 IPMP)";
    streamMap[0x1b] = "H.264 Video";
    streamMap[0x1c] = "ISO/IEC 14496-3 Audio";
    streamMap[0x1d] = "ISO/IEC 14496-17 Text";
    streamMap[0x1e] = "Auxiliary video stream as defined in ISO/IEC 23002-3";
    streamMap[0x1f] = "SVC video sub-bitstream of an AVC video stream";
    streamMap[0x20] = "MVC video sub-bitstream of an AVC video stream";
    streamMap[0x21] = "Video stream as defined in Rec. ITU-T T.800 | ISO/IEC 15444-1";
    streamMap[0x22] = "Video stream for stereoscopic 3D services Rec. ITU-T H.262 | ISO/IEC 13818-2";
    streamMap[0x23] = "Video stream for stereoscopic 3D services Rec. ITU-T H.264 | ISO/IEC 14496-10";
    streamMap[0x24] = "HEVC video bitstream Rec. ITU-T H.265 | ISO/IEC 23008-2";
    streamMap[0x25] = "HEVC video bitstream of profile in Annex A Rec. ITU-T H.265 | ISO/IEC 23008-2";
    streamMap[0x26] = "AVC MVCD video sub-bitstream of profile defined in Annex I of Rec. ITU-T H.264 | ISO/IEC 14496-10";
    streamMap[0x27] = "Timeline and External Media Information Stream";
    streamMap[0x28] = "HEVC Annex G profile TemporalID0";
    streamMap[0x29] = "HEVC Annex G profile";
    streamMap[0x2A] = "HEVC Annex H profile TemporalID0";
    streamMap[0x2B] = "HEVC Annex H profile";
    streamMap[0x2C] = "Green access units carried in MPEG-2 sections";
    streamMap[0x2D] = "ISO/IEC 23008-3 Audio with MHAS transport syntax – main stream";
    streamMap[0x2E] = "ISO/IEC 23008-3 Audio with MHAS transport syntax – auxiliary stream";
    streamMap[0x2F] = "Quality access units carried in sections";
    streamMap[0x30] = "Media Orchestration Access Units carried in sections";
    streamMap[0x31] = "HEVC Motion Constrained Tile Set, parameter sets, slice headers";

    for (int i = 0x32; i < 0x7F; i++)
    {
        streamMap[i] = "ISO 13818-1 reserved";
    }

    streamMap[0x7F] = "IPMP Stream";
    streamMap[0x80] = "DigiCipher II Video";
    streamMap[0x81] = "A52 / AC-3 Audio";
    streamMap[0x82] = "HDMV DTS Audio";
    streamMap[0x83] = "LPCM Audio";
    streamMap[0x84] = "SDDS Audio";
    streamMap[0x85] = "ATSC Program ID";
    streamMap[0x86] = "DTS-HD Audio";
    streamMap[0x87] = "E-AC- 3 Audio";
    streamMap[0x8a] = "DTS Audio";
    streamMap[0x91] = "A52b / AC-3 Audio";
    streamMap[0x92] = "DVD_SPU vls Subtitle";
    streamMap[0x94] = "SDDS Audio";
    streamMap[0xa0] = "MSCODEC Video";
    streamMap[0xea] = "Private ES(VC-1)";
}

size_t mptsParser::pushVideoData(uint8_t *p, size_t size)
{
    if(m_videoDataSize + size > m_videoBufferSize)
    {
        m_videoBufferSize += VIDEO_DATA_MEMORY_INCREMENT;
        m_pVideoData = (uint8_t*) realloc((void*) m_pVideoData, m_videoBufferSize);
    }

    std::memcpy(m_pVideoData + m_videoDataSize, p, size);
    m_videoDataSize += size;

    return m_videoDataSize;
}

// Returns the amount of bytes left in the video buffer after compacting
size_t mptsParser::compactVideoData(size_t bytesToCompact)
{
    size_t bytes_leftover = m_videoDataSize - bytesToCompact;

    if(bytes_leftover > 0)
    {
        std::memcpy(m_pVideoData, m_pVideoData + bytesToCompact, bytes_leftover);
        m_videoDataSize = bytes_leftover;
    }

    return bytes_leftover;
}

size_t mptsParser::getVideoDataSize()
{
    return m_videoDataSize;
}

size_t mptsParser::popVideoData()
{
    size_t ret = m_videoDataSize;
    if(m_pVideoData)
    {
        free(m_pVideoData);
        m_pVideoData = NULL;
    }

    m_videoDataSize = 0;
    m_videoBufferSize = 0;
    
    return ret;
}

size_t mptsParser::readPAT(uint8_t*& p, program_association_table& pat, bool payloadUnitStart)
{
    uint8_t* p_start = p;
    pat.payload_start_offset = 0;
    pat.payload_unit_start = payloadUnitStart;

    if (payloadUnitStart)
    {
        pat.payload_start_offset = *p; // Spec 2.4.4.1
        incPtr(p, 1);
        incPtr(p, pat.payload_start_offset);
    }

    pat.table_id = *p;
    incPtr(p, 1);
    pat.section_length = util::read2Bytes(p);
    incPtr(p, 2);
    pat.section_syntax_indicator = (0x8000 & pat.section_length) >> 15;
    pat.section_length &= 0xFFF;

    uint8_t* p_section_start = p;

    pat.transport_stream_id = util::read2Bytes(p);
    incPtr(p, 2);

    pat.current_next_indicator = *p;
    incPtr(p, 1);
    pat.version_number = (pat.current_next_indicator & 0x3E) >> 1;
    pat.current_next_indicator &= 0x1;

    pat.section_number = *p;
    incPtr(p, 1);
    pat.last_section_number = *p;
    incPtr(p, 1);

    while ((p - p_section_start) < (pat.section_length - 4))
    {
        uint16_t program_number = util::read2Bytes(p);
        incPtr(p, 2);

        uint16_t pid = util::read2Bytes(p);
        pid &= 0x1FFF;
        incPtr(p, 2);

        pat.program_numbers.emplace_back(program_number, pid);
    }

    return p - p_start;
}

// 2.4.4.3 Program association Table
//
// The Program Association Table provides the correspondence between a program_number and the PID value of the
// Transport Stream packets which carry the program definition.The program_number is the numeric label associated with
// a program.
size_t mptsParser::readPAT(uint8_t *&p, bool payloadUnitStart)
{
    uint8_t* p_start = p;
    uint8_t payload_start_offset = 0;

    if(payloadUnitStart)
    {
        payload_start_offset = *p; // Spec 2.4.4.1
        incPtr(p, 1);
        incPtr(p, payload_start_offset);
    }

    uint8_t table_id = *p;
    incPtr(p, 1);
    uint16_t section_length = util::read2Bytes(p);
    incPtr(p, 2);
    uint8_t section_syntax_indicator = (0x8000 & section_length) >> 15;
    section_length &= 0xFFF;

    uint8_t *p_section_start = p;

    uint16_t transport_stream_id = util::read2Bytes(p);
    incPtr(p, 2);

    uint8_t current_next_indicator = *p;
    incPtr(p, 1);
    uint8_t version_number = (current_next_indicator & 0x3E) >> 1;
    current_next_indicator &= 0x1;

    uint8_t section_number = *p;
    incPtr(p, 1);
    uint8_t last_section_number = *p;
    incPtr(p, 1);

    printfXml(2, "<program_association_table>\n");
    if(payloadUnitStart)
        printfXml(3, "<pointer_field>0x%x</pointer_field>\n", payload_start_offset);
    printfXml(3, "<table_id>0x%x</table_id>\n", table_id);
    printfXml(3, "<section_syntax_indicator>%d</section_syntax_indicator>\n", section_syntax_indicator);
    printfXml(3, "<section_length>%d</section_length>\n", section_length);
    printfXml(3, "<transport_stream_id>0x%x</transport_stream_id>\n", transport_stream_id);
    printfXml(3, "<version_number>0x%x</version_number>\n", version_number);
    printfXml(3, "<current_next_indicator>0x%x</current_next_indicator>\n", current_next_indicator);
    printfXml(3, "<section_number>0x%x</section_number>\n", section_number);
    printfXml(3, "<last_section_number>0x%x</last_section_number>\n", last_section_number);

    while ((p - p_section_start) < (section_length - 4))
    {
        m_programNumber = util::read2Bytes(p);
        incPtr(p, 2);
        uint16_t network_pid = 0;

        if (0 == m_programNumber)
        {
            network_pid = util::read2Bytes(p);
            incPtr(p, 2);
            network_pid &= 0x1FFF;
            m_networkPid = network_pid;
        }
        else
        {
            m_programMapPid = util::read2Bytes(p);
            incPtr(p, 2);
            m_programMapPid &= 0x1FFF;
        }

        printfXml(3, "<program>\n");
        printfXml(4, "<number>%d</number>\n", m_programNumber);

        if(network_pid)
            printfXml(4, "<network_pid>0x%x</network_pid>\n", m_networkPid);
        else
            printfXml(4, "<program_map_pid>0x%x</program_map_pid>\n", m_programMapPid);

        printfXml(3, "</program>\n");
    }

    printfXml(2, "</program_association_table>\n");

    return p - p_start;
}

// 2.4.4.9 Program Map Table
//
// The Program Map Table provides the mappings between program numbers and the program elements that comprise
// them. A single instance of such a mapping is referred to as a "program definition". The program map table is the
// complete collection of all program definitions for a Transport Stream.
size_t mptsParser::readPMT(uint8_t*& p, program_map_table& pmt, bool payloadUnitStart)
{
    uint8_t* p_start = p;

    pmt.payload_start_offset = 0;
    pmt.payload_unit_start = payloadUnitStart;

    if (payloadUnitStart)
    {
        pmt.payload_start_offset = *p; // Spec 2.4.4.1
        incPtr(p, 1);
        incPtr(p, pmt.payload_start_offset);
    }

    pmt.table_id = *p;
    incPtr(p, 1);
    pmt.section_length = util::read2Bytes(p);
    incPtr(p, 2);
    pmt.section_syntax_indicator = pmt.section_length & 0x80 >> 15;
    pmt.section_length &= 0xFFF;

    uint8_t* p_section_start = p;

    pmt.program_number = util::read2Bytes(p);
    incPtr(p, 2);

    pmt.current_next_indicator = *p;
    incPtr(p, 1);
    pmt.version_number = (pmt.current_next_indicator & 0x3E) >> 1;
    pmt.current_next_indicator &= 0x1;

    pmt.section_number = *p;
    incPtr(p, 1);
    pmt.last_section_number = *p;
    incPtr(p, 1);

    pmt.pcr_pid = util::read2Bytes(p);
    incPtr(p, 2);
    pmt.pcr_pid &= 0x1FFF;

    pmt.program_info_length = util::read2Bytes(p);
    incPtr(p, 2);

    pmt.program_info_length &= 0x3FF;

    p += readElementDescriptors(p, pmt);

    // Subtract 4 from section_length to account for 4 byte CRC at its end.  The CRC is not program data.
    while ((p - p_section_start) < (pmt.section_length - 4))
    {
        uint8_t stream_type = *p;
        incPtr(p, 1);
        uint16_t elementary_pid = util::read2Bytes(p);
        incPtr(p, 2);
        elementary_pid &= 0x1FFF;

        uint16_t es_info_length = util::read2Bytes(p);
        incPtr(p, 2);
        es_info_length &= 0xFFF;

        p += es_info_length;

        pmt.program_elements.emplace_back(stream_type, elementary_pid, es_info_length);
    }

    return p - p_start;
}

// 2.4.4.9 Program Map Table
//
// The Program Map Table provides the mappings between program numbers and the program elements that comprise
// them. A single instance of such a mapping is referred to as a "program definition". The program map table is the
// complete collection of all program definitions for a Transport Stream.
size_t mptsParser::readPMT(uint8_t *&p, bool payloadUnitStart)
{
    uint8_t* p_start = p;
    uint8_t payload_start_offset = 0;

    if(payloadUnitStart)
    {
        payload_start_offset = *p; // Spec 2.4.4.1
        incPtr(p, 1);
        incPtr(p, payload_start_offset);
    }

    uint8_t table_id = *p;
    incPtr(p, 1);
    uint16_t section_length = util::read2Bytes(p);
    incPtr(p, 2);
    uint8_t section_syntax_indicator = section_length & 0x80 >> 15;
    section_length &= 0xFFF;

    uint8_t *p_section_start = p;

    uint16_t program_number = util::read2Bytes(p);
    incPtr(p, 2);

    uint8_t current_next_indicator = *p;
    incPtr(p, 1);
    uint8_t version_number = (current_next_indicator & 0x3E) >> 1;
    current_next_indicator &= 0x1;

    uint8_t section_number = *p;
    incPtr(p, 1);
    uint8_t last_section_number = *p;
    incPtr(p, 1);

    uint16_t pcr_pid = util::read2Bytes(p);
    incPtr(p, 2);
    pcr_pid &= 0x1FFF;

    m_pidToNameMap[pcr_pid] = "PCR";

    uint16_t program_info_length = util::read2Bytes(p);
    incPtr(p, 2);

    program_info_length &= 0x3FF;
    
    printfXml(2, "<program_map_table>\n");
    if(payloadUnitStart)
        printfXml(3, "<pointer_field>0x%x</pointer_field>\n", payload_start_offset);
    printfXml(3, "<table_id>0x%x</table_id>\n", table_id);
    printfXml(3, "<section_syntax_indicator>%d</section_syntax_indicator>\n", section_syntax_indicator);
    printfXml(3, "<section_length>%d</section_length>\n", section_length);
    printfXml(3, "<program_number>%d</program_number>\n", program_number);
    printfXml(3, "<version_number>%d</version_number>\n", version_number);
    printfXml(3, "<current_next_indicator>%d</current_next_indicator>\n", current_next_indicator);
    printfXml(3, "<section_number>%d</section_number>\n", section_number);
    printfXml(3, "<last_section_number>%d</last_section_number>\n", last_section_number);
    printfXml(3, "<pcr_pid>0x%x</pcr_pid>\n", pcr_pid);
    printfXml(3, "<program_info_length>%d</program_info_length>\n", program_info_length);

    p += readElementDescriptors(p, program_info_length);

    //my_printf("program_number:%d, pcr_pid:%x\n", program_number, pcr_pid);
    //my_printf("  Elementary Streams:\n");

    std::map <uint16_t, char *> streamMap; // ID, name
    initStreamTypes(streamMap);

    // This has to be done by hand
    m_pidToNameMap[0x1FFF] = "NULL Packet";

    size_t stream_count = 0;

    // Subtract 4 from section_length to account for 4 byte CRC at its end.  The CRC is not program data.
    while((p - p_section_start) < (section_length - 4))
    {
        uint8_t stream_type = *p;
        incPtr(p, 1);
        uint16_t elementary_pid = util::read2Bytes(p);
        incPtr(p, 2);
        elementary_pid &= 0x1FFF;

        uint16_t es_info_length = util::read2Bytes(p);
        incPtr(p, 2);
        es_info_length &= 0xFFF;

        p += es_info_length;

        // Scte35 stream type is 0x86
        if(0x86 == stream_type)
            m_scte35Pid = elementary_pid;

        m_pidToNameMap[elementary_pid] = streamMap[stream_type];
        m_pidToTypeMap[elementary_pid] = (eMptsStreamType) stream_type;

        //my_printf("    %d) pid:%x, stream_type:%x (%s)\n", stream_count++, elementary_pid, stream_type, streamMap[stream_type]);

        printfXml(3, "<stream>\n");
        printfXml(4, "<number>%zd</number>\n", stream_count);
        printfXml(4, "<pid>0x%x</pid>\n", elementary_pid);
        printfXml(4, "<type_number>0x%x</type_number>\n", stream_type);
        printfXml(4, "<type_name>%s</type_name>\n", streamMap[stream_type]);
        printfXml(3, "</stream>\n");

        stream_count++;
    }

    printfXml(2, "</program_map_table>\n");

    return p - p_start;
}

void mptsParser::printElementDescriptors(const program_map_table& pmt)
{
    unsigned int descriptorNumber = 0;
    for (const auto ped : pmt.program_element_descriptors)
    {
        printfXml(3, "<descriptor>\n");
        printfXml(4, "<number>%d</number>\n", descriptorNumber++);
        printfXml(4, "<tag>%d</tag>\n", ped.descriptor_tag);
        printfXml(4, "<length>%d</length>\n", ped.descriptor_length);

        if (auto value = std::get_if<std::shared_ptr<video_stream_descriptor>>(ped.descriptor.get()))
        {
            auto pVd = value->get();
            printfXml(4, "<type>video_stream_descriptor</type>\n");
            printfXml(4, "<multiple_frame_rate_flag>%d</multiple_frame_rate_flag>\n", pVd->multiple_frame_rate_flag);
            printfXml(4, "<frame_rate_code>0x%x</frame_rate_code>\n", pVd->frame_rate_code);
            printfXml(4, "<mpeg_1_only_flag>%d</mpeg_1_only_flag>\n", pVd->mpeg_1_only_flag);
            printfXml(4, "<constrained_parameter_flag>%d</constrained_parameter_flag>\n", pVd->constrained_parameter_flag);
            printfXml(4, "<still_picture_flag>%d</still_picture_flag>\n", pVd->still_picture_flag);

            if (!pVd->mpeg_1_only_flag)
            {
                printfXml(4, "<profile_and_level_indication>0x%x</profile_and_level_indication>\n", pVd->profile_and_level_indication);
                printfXml(4, "<chroma_format>%d</chroma_format>\n", pVd->chroma_format);
                printfXml(4, "<frame_rate_extension_flag>%d</frame_rate_extension_flag>\n", pVd->frame_rate_extension_flag);
            }
        }
        else if (auto value = std::get_if<std::shared_ptr<audio_stream_descriptor>>(ped.descriptor.get()))
        {
            auto pAd = value->get();
            printfXml(4, "<type>audio_stream_descriptor</type>\n");
            printfXml(4, "<free_format_flag>%d</free_format_flag>\n", pAd->free_format_flag);
            printfXml(4, "<id>%d</id>\n", pAd->id);
            printfXml(4, "<layer>%d</layer>\n", pAd->layer);
            printfXml(4, "<variable_rate_audio_indicator>%d</variable_rate_audio_indicator>\n", pAd->variable_rate_audio_indicator);
        }
        else if (auto value = std::get_if<std::shared_ptr<registration_descriptor>>(ped.descriptor.get()))
        {
            auto pRd = value->get();
            char sz_temp[5];
            char* pChar = (char*)&(pRd->format_identifier);
            sz_temp[3] = *pChar++;
            sz_temp[2] = *pChar++;
            sz_temp[1] = *pChar++;
            sz_temp[0] = *pChar;
            sz_temp[4] = 0;
            printfXml(4, "<type>registration_descriptor</type>\n");
            printfXml(4, "<format_identifier>%s</format_identifier>\n", sz_temp);
        }

        printfXml(3, "</descriptor>\n");
    }
}

// 2.6 Program and program element descriptors
// Program and program element descriptors are structures which may be used to extend the definitions of programs and
// program elements.All descriptors have a format which begins with an 8 - bit tag value.The tag value is followed by an
// 8 - bit descriptor length and data fields.
size_t mptsParser::readElementDescriptors(uint8_t* p, program_map_table& pmt)
{
    uint8_t* p_start = p;
    uint32_t descriptor_number = 0;
    uint32_t scte35_format_identifier = 0;

    // PUSH BACK program_element_descriptors here
    program_element_descriptor ped;

    while (p - p_start < pmt.program_info_length)
    {
        ped.reset();

        ped.descriptor_tag = *p;
        incPtr(p, 1);
        ped.descriptor_length = *p;
        incPtr(p, 1);

        //printfXml(3, "<descriptor>\n");
        //printfXml(4, "<number>%d</number>\n", descriptor_number);
        //printfXml(4, "<tag>%d</tag>\n", ped.descriptor_tag);
        //printfXml(4, "<length>%d</length>\n", ped.descriptor_length);

        switch (ped.descriptor_tag)
        {
        case VIDEO_STREAM_DESCRIPTOR:
        {
            /*
                video_stream_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    multiple_frame_rate_flag 1 bslbf
                    frame_rate_code 4 uimsbf
                    mpeg_1_only_flag 1 bslbf
                    constrained_parameter_flag 1 bslbf
                    still_picture_flag 1 bslbf
                    if (MPEG_1_only_flag = = '0') {
                        profile_and_level_indication 8 uimsbf
                        chroma_format 2 uimsbf
                        frame_rate_extension_flag 1 bslbf
                        Reserved 5 bslbf
                    }
                }
            */
            
            std::shared_ptr<video_stream_descriptor> pVd = std::make_shared<video_stream_descriptor>();

            pVd->multiple_frame_rate_flag = *p;
            incPtr(p, 1);
            pVd->frame_rate_code = (pVd->multiple_frame_rate_flag & 0x78) >> 3;
            pVd->mpeg_1_only_flag = (pVd->multiple_frame_rate_flag & 0x04) >> 2;
            pVd->constrained_parameter_flag = (pVd->multiple_frame_rate_flag & 0x02) >> 1;
            pVd->still_picture_flag = (pVd->multiple_frame_rate_flag & 0x01);
            pVd->multiple_frame_rate_flag >>= 7;

            if (!pVd->mpeg_1_only_flag)
            {
                pVd->profile_and_level_indication = *p;
                incPtr(p, 1);
                pVd->chroma_format = *p;
                incPtr(p, 1);
                pVd->frame_rate_extension_flag = (pVd->chroma_format & 0x10) >> 4;
                pVd->chroma_format >>= 6;
            }

            *ped.descriptor = std::move(pVd);
        }
        break;
        case AUDIO_STREAM_DESCRIPTOR:
        {
            /*
                audio_stream_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    free_format_flag 1 bslbf
                    ID 1 bslbf
                    layer 2 bslbf
                    variable_rate_audio_indicator 1 bslbf
                    reserved 3 bslbf
                }
            */

            std::shared_ptr<audio_stream_descriptor> pAd = std::make_shared<audio_stream_descriptor>();

            pAd->free_format_flag = *p;
            incPtr(p, 1);
            pAd->id = (pAd->free_format_flag & 0x40) >> 6;
            pAd->layer = (pAd->free_format_flag & 0x30) >> 4;
            pAd->variable_rate_audio_indicator = (pAd->free_format_flag & 0x08) >> 3;
            pAd->free_format_flag >>= 7;

            *ped.descriptor = std::move(pAd);
        }
        break;
        case HIERARCHY_DESCRIPTOR:
        {
            /*
                hierarchy_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    reserved 4 bslbf
                    hierarchy_type 4 uimsbf
                    reserved 2 bslbf
                    hierarchy_layer_index 6 uimsbf
                    reserved 2 bslbf
                    hierarchy_embedded_layer_index 6 uimsbf
                    reserved 2 bslbf
                    hierarchy_channel 6 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>hierarchy_descriptor</type>\n");
        }
        break;
        case REGISTRATION_DESCRIPTOR:
        {
            /*
                registration_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    format_identifier 32 uimsbf
                    for (i = 0; i < N; i++){
                        additional_identification_info 8 bslbf
                    }
                }
            */

            std::shared_ptr<registration_descriptor> pRd = std::make_shared<registration_descriptor>();

            pRd->format_identifier = util::read4Bytes(p);
            incPtr(p, 4);

//            if (0x43554549 == pRd->format_identifier)
//                scte35_format_identifier = format_identifier; // Should be 0x43554549 (ASCII CUEI)

            incPtr(p, ped.descriptor_length - 4);

            ped.descriptor = std::make_shared<mpts_descriptor>();
            *ped.descriptor = std::move(pRd);
        }
        break;
        case DATA_STREAM_ALIGNMENT_DESCRIPTOR:
        {
            /*
                data_stream_alignment_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    alignment_type 8 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>data_stream_alignment_descriptor</type>\n");
        }
        break;
        case TARGET_BACKGROUND_GRID_DESCRIPTOR:
        {
            /*
                target_background_grid_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    horizontal_size 14 uimsbf
                    vertical_size 14 uimsbf
                    aspect_ratio_information 4 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>target_background_grid_descriptor</type>\n");
        }
        break;
        case VIDEO_WINDOW_DESCRIPTOR:
        {
            /*
                video_window_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    horizontal_offset 14 uimsbf
                    vertical_offset 14 uimsbf
                    window_priority 4 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>video_window_descriptor</type>\n");
        }
        break;
        case CA_DESCRIPTOR:
        {
            /*
                CA_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    CA_system_ID 16 uimsbf
                    reserved 3 bslbf
                    CA_PID 13 uimsbf
                    for (i = 0; i < N; i++) {
                        private_data_byte 8 uimsbf
                    }
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>ca_descriptor</type>\n");
        }
        break;
        case ISO_639_LANGUAGE_DESCRIPTOR:
        {
            /*
                ISO_639_language_descriptor() {
                descriptor_tag 8 uimsbf
                descriptor_length 8 uimsbf
                for (i = 0; i < N; i++) {
                    ISO_639_language_code 24 bslbf
                    audio_type 8 bslbf
                }
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>iso_639_language_descriptor</type>\n");
        }
        break;
        case SYSTEM_CLOCK_DESCRIPTOR:
        {
            /*
                system_clock_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    external_clock_reference_indicator 1 bslbf
                    reserved 1 bslbf
                    clock_accuracy_integer 6 uimsbf
                    clock_accuracy_exponent 3 uimsbf
                    reserved 5 bslbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>system_clock_descriptor</type>\n");
        }
        break;
        case MULTIPLEX_BUFFER_UTILIZATION_DESCRIPTOR:
        {
            /*
                Multiplex_buffer_utilization_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    bound_valid_flag 1 bslbf
                    LTW_offset_lower_bound 15 uimsbf
                    reserved 1 bslbf
                    LTW_offset_upper_bound 15 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>multiplex_buffer_utilization_descriptor</type>\n");
        }
        break;
        case COPYRIGHT_DESCRIPTOR:
        {
            /*
                copyright_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    copyright_identifier 32 uimsbf
                    for (i = 0; i < N; i++){
                        additional_copyright_info 8 bslbf
                    }
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>copyright_descriptor</type>\n");
        }
        break;
        case MAXIMUM_BITRATE_DESCRIPTOR:
        {
            /*
                maximum_bitrate_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    reserved 2 bslbf
                    maximum_bitrate 22 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>maximum_bitrate_descriptor</type>\n");
        }
        break;
        case PRIVATE_DATA_INDICATOR_DESCRIPTOR:
        {
            /*
                private_data_indicator_descriptor() {
                    descriptor_tag 38 uimsbf
                    descriptor_length 38 uimsbf
                    private_data_indicator 32 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>private_data_indicator_descriptor</type>\n");
        }
        break;
        case SMOOTHING_BUFFER_DESCRIPTOR:
        {
            /*
                smoothing_buffer_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    reserved 2 bslbf
                    sb_leak_rate 22 uimsbf
                    reserved 2 bslbf
                    sb_size 22 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>smoothing_buffer_descriptor</type>\n");
        }
        break;
        case STD_DESCRIPTOR:
        {
            /*
                STD_descriptor () {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    reserved 7 bslbf
                    leak_valid_flag 1 bslbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>std_descriptor</type>\n");
        }
        case IBP_DESCRIPTOR:
        {
            /*
                ibp_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    closed_gop_flag 1 uimsbf
                    identical_gop_flag 1 uimsbf
                    max_gop-length 14 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>ibp_descriptor</type>\n");
        }
        break;
        case MPEG_4_VIDEO_DESCRIPTOR:
        {
            /*
                MPEG-4_video_descriptor () {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    MPEG-4_visual_profile_and_level 8 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>mpeg_4_video_descriptor</type>\n");
        }
        break;
        case MPEG_4_AUDIO_DESCRIPTOR:
        {
            /*
                MPEG-4_audio_descriptor () {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    MPEG-4_audio_profile_and_level 8 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>mpeg_4_audio_descriptor</type>\n");
        }
        break;
        case IOD_DESCRIPTOR:
        {
            /*
                IOD_descriptor () {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    Scope_of_IOD_label 8 uimsbf
                    IOD_label 8 uimsbf
                    InitialObjectDescriptor () 8 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>iod_descriptor</type>\n");
        }
        break;
        case SL_DESCRIPTOR:
        {
            /*
                SL_descriptor () {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    ES_ID 16 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>sl_descriptor</type>\n");
        }
        break;
        case FMC_DESCRIPTOR:
        {
            /*
                FMC_descriptor () {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    for (i = 0; i < descriptor_length; i + = 3) {
                        ES_ID 16 uimsbf
                        FlexMuxChannel 8 uimsbf
                    }
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>fmc_descriptor</type>\n");
        }
        break;
        case EXTERNAL_ES_ID_DESCRIPTOR:
        {
            /*
                External_ES_ID_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    External_ES_ID 16 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>external_es_id_descriptor</type>\n");
        }
        break;
        case MUXCODE_DESCRIPTOR:
        {
            /*
                Muxcode_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    for (i = 0; i < N; i++) {
                        MuxCodeTableEntry ()
                    }
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>muxcode_descriptor</type>\n");
        }
        break;
        case FMXBUFFERSIZE_DESCRIPTOR:
        {
            /*
                FmxBufferSize_descriptor () {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    DefaultFlexMuxBufferDescriptor()
                    for (i=0; i<descriptor_length; i += 4) {
                        FlexMuxBufferDescriptor()
                    }
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>fmxbuffersize_descriptor</type>\n");
        }
        break;
        case MULTIPLEXBUFFER_DESCRIPTOR:
        {
            /*
                MultiplexBuffer_descriptor () {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    MB_buffer_size 24 uimsbf
                    TB_leak_rate 24 uimsbf
                }
            */
            incPtr(p, ped.descriptor_length); // TODO
            printfXml(4, "<type>multiplexbuffer_descriptor</type>\n");
        }
        break;
        default:
            incPtr(p, ped.descriptor_length);
        }

        //printfXml(3, "</descriptor>\n");

        pmt.program_element_descriptors.push_back(ped);

        descriptor_number++;
    }

    return p - p_start;
}

// 2.6 Program and program element descriptors
// Program and program element descriptors are structures which may be used to extend the definitions of programs and
// program elements.All descriptors have a format which begins with an 8 - bit tag value.The tag value is followed by an
// 8 - bit descriptor length and data fields.
size_t mptsParser::readElementDescriptors(uint8_t *p, uint16_t programInfoLength)
{
    uint8_t* p_start = p;
    uint32_t descriptor_number = 0;
    uint8_t descriptor_length = 0;
    uint32_t scte35_format_identifier = 0;

    while(p - p_start < programInfoLength)
    {
        uint8_t descriptor_tag = *p;
        incPtr(p, 1);
        descriptor_length = *p;
        incPtr(p, 1);

        printfXml(3, "<descriptor>\n");
        printfXml(4, "<number>%d</number>\n", descriptor_number);
        printfXml(4, "<tag>%d</tag>\n", descriptor_tag);
        printfXml(4, "<length>%d</length>\n", descriptor_length);

        switch(descriptor_tag)
        {
            case VIDEO_STREAM_DESCRIPTOR:
            {
                /*
                    video_stream_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        multiple_frame_rate_flag 1 bslbf
                        frame_rate_code 4 uimsbf
                        MPEG_1_only_flag 1 bslbf
                        constrained_parameter_flag 1 bslbf
                        still_picture_flag 1 bslbf
                        if (MPEG_1_only_flag = = '0') {
                            profile_and_level_indication 8 uimsbf
                            chroma_format 2 uimsbf
                            frame_rate_extension_flag 1 bslbf
                            Reserved 5 bslbf
                        }
                    }
                */

                uint8_t multiple_frame_rate_flag = *p;
                incPtr(p, 1);
                uint8_t frame_rate_code = (multiple_frame_rate_flag & 0x78) >> 3;
                uint8_t mpeg_1_only_flag = (multiple_frame_rate_flag & 0x04) >> 2;
                uint8_t constrained_parameter_flag = (multiple_frame_rate_flag & 0x02) >> 1;
                uint8_t still_picture_flag = (multiple_frame_rate_flag & 0x01);
                multiple_frame_rate_flag >>= 7;

                printfXml(4, "<type>video_stream_descriptor</type>\n");
                printfXml(4, "<multiple_frame_rate_flag>%d</multiple_frame_rate_flag>\n", multiple_frame_rate_flag);
                printfXml(4, "<frame_rate_code>0x%x</frame_rate_code>\n", frame_rate_code);
                printfXml(4, "<mpeg_1_only_flag>%d</mpeg_1_only_flag>\n", mpeg_1_only_flag);
                printfXml(4, "<constrained_parameter_flag>%d</constrained_parameter_flag>\n", constrained_parameter_flag);
                printfXml(4, "<still_picture_flag>%d</still_picture_flag>\n", still_picture_flag);

                if (!mpeg_1_only_flag)
                {
                    uint8_t profile_and_level_indication = *p;
                    incPtr(p, 1);
                    uint8_t chroma_format = *p;
                    incPtr(p, 1);
                    uint8_t frame_rate_extension_flag = (chroma_format & 0x10) >> 4;
                    chroma_format >>= 6;
                    
                    printfXml(4, "<profile_and_level_indication>0x%x</profile_and_level_indication>\n", profile_and_level_indication);
                    printfXml(4, "<chroma_format>%d</chroma_format>\n", chroma_format);
                    printfXml(4, "<frame_rate_extension_flag>%d</frame_rate_extension_flag>\n", frame_rate_extension_flag);
                }
            }
            break;
            case AUDIO_STREAM_DESCRIPTOR:
            {
                /*
                    audio_stream_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        free_format_flag 1 bslbf
                        ID 1 bslbf
                        layer 2 bslbf
                        variable_rate_audio_indicator 1 bslbf
                        reserved 3 bslbf
                    }
                */

                uint8_t free_format_flag = *p;
                incPtr(p, 1);
                uint8_t id = (free_format_flag & 0x40) >> 6;
                uint8_t layer = (free_format_flag & 0x30) >> 4;
                uint8_t variable_rate_audio_indicator = (free_format_flag & 0x08) >> 3;
                free_format_flag >>= 7;

                printfXml(4, "<type>audio_stream_descriptor</type>\n");
                printfXml(4, "<free_format_flag>%d</free_format_flag>\n", free_format_flag);
                printfXml(4, "<id>%d</id>\n", id);
                printfXml(4, "<layer>%d</layer>\n", layer);
                printfXml(4, "<variable_rate_audio_indicator>%d</variable_rate_audio_indicator>\n", variable_rate_audio_indicator);
            }
            break;
            case HIERARCHY_DESCRIPTOR:
            {
                /*
                    hierarchy_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        reserved 4 bslbf
                        hierarchy_type 4 uimsbf
                        reserved 2 bslbf
                        hierarchy_layer_index 6 uimsbf
                        reserved 2 bslbf
                        hierarchy_embedded_layer_index 6 uimsbf
                        reserved 2 bslbf
                        hierarchy_channel 6 uimsbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>hierarchy_descriptor</type>\n");
            }
            break;
            case REGISTRATION_DESCRIPTOR:
            {
                /*
                    registration_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        format_identifier 32 uimsbf
                        for (i = 0; i < N; i++){
                            additional_identification_info 8 bslbf
                        }
                    }
                */

                uint32_t format_identifier = util::read4Bytes(p);
                incPtr(p, 4);

                if(0x43554549 == format_identifier)
                    scte35_format_identifier = format_identifier; // Should be 0x43554549 (ASCII CUEI)

                incPtr(p, descriptor_length - 4);

                char sz_temp[5];
                char *pChar = (char *) &format_identifier;
                sz_temp[3] = *pChar++;
                sz_temp[2] = *pChar++;
                sz_temp[1] = *pChar++;
                sz_temp[0] = *pChar;
                sz_temp[4] = 0;
                printfXml(4, "<type>registration_descriptor</type>\n");
                printfXml(4, "<format_identifier>%s</format_identifier>\n", sz_temp);
                
                //if (0 != scte35_format_identifier)
                //    my_printf("        <scte35_format_identifier>0x%x</scte35_format_identifier>\n", scte35_format_identifier);
            }
            break;
            case DATA_STREAM_ALIGNMENT_DESCRIPTOR:
            {
                /*
                    data_stream_alignment_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        alignment_type 8 uimsbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>data_stream_alignment_descriptor</type>\n");
            }
            break;
            case TARGET_BACKGROUND_GRID_DESCRIPTOR:
            {
                /*
                    target_background_grid_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        horizontal_size 14 uimsbf
                        vertical_size 14 uimsbf
                        aspect_ratio_information 4 uimsbf
                    }            
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>target_background_grid_descriptor</type>\n");
            }
            break;
            case VIDEO_WINDOW_DESCRIPTOR:
            {
                /*
                    video_window_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        horizontal_offset 14 uimsbf
                        vertical_offset 14 uimsbf
                        window_priority 4 uimsbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>video_window_descriptor</type>\n");
            }
            break;
            case CA_DESCRIPTOR:
            {
                /*
                    CA_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        CA_system_ID 16 uimsbf
                        reserved 3 bslbf
                        CA_PID 13 uimsbf
                        for (i = 0; i < N; i++) {
                            private_data_byte 8 uimsbf
                        }
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>ca_descriptor</type>\n");
            }
            break;
            case ISO_639_LANGUAGE_DESCRIPTOR:
            {
                /*
                    ISO_639_language_descriptor() {
                    descriptor_tag 8 uimsbf
                    descriptor_length 8 uimsbf
                    for (i = 0; i < N; i++) {
                        ISO_639_language_code 24 bslbf
                        audio_type 8 bslbf
                    }
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>iso_639_language_descriptor</type>\n");
            }
            break;
            case SYSTEM_CLOCK_DESCRIPTOR:
            {
                /*
                    system_clock_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        external_clock_reference_indicator 1 bslbf
                        reserved 1 bslbf
                        clock_accuracy_integer 6 uimsbf
                        clock_accuracy_exponent 3 uimsbf
                        reserved 5 bslbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>system_clock_descriptor</type>\n");
            }
            break;
            case MULTIPLEX_BUFFER_UTILIZATION_DESCRIPTOR:
            {
                /*
                    Multiplex_buffer_utilization_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        bound_valid_flag 1 bslbf
                        LTW_offset_lower_bound 15 uimsbf
                        reserved 1 bslbf
                        LTW_offset_upper_bound 15 uimsbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>multiplex_buffer_utilization_descriptor</type>\n");
            }
            break;
            case COPYRIGHT_DESCRIPTOR:
            {
                /*
                    copyright_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        copyright_identifier 32 uimsbf
                        for (i = 0; i < N; i++){
                            additional_copyright_info 8 bslbf
                        }
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>copyright_descriptor</type>\n");
            }
            break;
            case MAXIMUM_BITRATE_DESCRIPTOR:
            {
                /*
                    maximum_bitrate_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        reserved 2 bslbf
                        maximum_bitrate 22 uimsbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>maximum_bitrate_descriptor</type>\n");
            }
            break;
            case PRIVATE_DATA_INDICATOR_DESCRIPTOR:
            {
                /*
                    private_data_indicator_descriptor() {
                        descriptor_tag 38 uimsbf
                        descriptor_length 38 uimsbf
                        private_data_indicator 32 uimsbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>private_data_indicator_descriptor</type>\n");
            }
            break;
            case SMOOTHING_BUFFER_DESCRIPTOR:
            {
                /*
                    smoothing_buffer_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        reserved 2 bslbf
                        sb_leak_rate 22 uimsbf
                        reserved 2 bslbf
                        sb_size 22 uimsbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>smoothing_buffer_descriptor</type>\n");
            }
            break;
            case STD_DESCRIPTOR:
            {
                /*
                    STD_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        reserved 7 bslbf
                        leak_valid_flag 1 bslbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>std_descriptor</type>\n");
            }
            case IBP_DESCRIPTOR:
            {
                /*
                    ibp_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        closed_gop_flag 1 uimsbf
                        identical_gop_flag 1 uimsbf
                        max_gop-length 14 uimsbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>ibp_descriptor</type>\n");
            }
            break;
            case MPEG_4_VIDEO_DESCRIPTOR:
            {
                /*
                    MPEG-4_video_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        MPEG-4_visual_profile_and_level 8 uimsbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>mpeg_4_video_descriptor</type>\n");
            }
            break;
            case MPEG_4_AUDIO_DESCRIPTOR:
            {
                /*
                    MPEG-4_audio_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        MPEG-4_audio_profile_and_level 8 uimsbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>mpeg_4_audio_descriptor</type>\n");
            }
            break;
            case IOD_DESCRIPTOR:
            {
                /*
                    IOD_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        Scope_of_IOD_label 8 uimsbf
                        IOD_label 8 uimsbf
                        InitialObjectDescriptor () 8 uimsbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>iod_descriptor</type>\n");
            }
            break;
            case SL_DESCRIPTOR:
            {
                /*
                    SL_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        ES_ID 16 uimsbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>sl_descriptor</type>\n");
            }
            break;
            case FMC_DESCRIPTOR:
            {
                /*
                    FMC_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        for (i = 0; i < descriptor_length; i + = 3) {
                            ES_ID 16 uimsbf
                            FlexMuxChannel 8 uimsbf
                        }
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>fmc_descriptor</type>\n");
            }
            break;
            case EXTERNAL_ES_ID_DESCRIPTOR:
            {
                /*
                    External_ES_ID_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        External_ES_ID 16 uimsbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>external_es_id_descriptor</type>\n");
            }
            break;
            case MUXCODE_DESCRIPTOR:
            {
                /*
                    Muxcode_descriptor() {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        for (i = 0; i < N; i++) {
                            MuxCodeTableEntry ()
                        }
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>muxcode_descriptor</type>\n");
            }
            break;
            case FMXBUFFERSIZE_DESCRIPTOR:
            {
                /*
                    FmxBufferSize_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        DefaultFlexMuxBufferDescriptor()
                        for (i=0; i<descriptor_length; i += 4) {
                            FlexMuxBufferDescriptor()
                        }
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>fmxbuffersize_descriptor</type>\n");
            }
            break;
            case MULTIPLEXBUFFER_DESCRIPTOR:
            {
                /*
                    MultiplexBuffer_descriptor () {
                        descriptor_tag 8 uimsbf
                        descriptor_length 8 uimsbf
                        MB_buffer_size 24 uimsbf
                        TB_leak_rate 24 uimsbf
                    }
                */
                incPtr(p, descriptor_length); // TODO
                printfXml(4, "<type>multiplexbuffer_descriptor</type>\n");
            }
            break;
            default:
                incPtr(p, descriptor_length);
        }

        printfXml(3, "</descriptor>\n");

        descriptor_number++;
    }

    return p - p_start;
}

// Table 2-21 PES Packet
// http://dvd.sourceforge.net/dvdinfo/pes-hdr.html
size_t mptsParser::processPESPacketHeader(uint8_t*& p, size_t PESPacketDataLength, PES_packet& pes_packet)
{
    pes_packet = { 0 };
    uint8_t* pStart = p;

    uint32_t fourBytes = util::read4Bytes(p);
    incPtr(p, 4);

    pes_packet.packet_start_code_prefix = (fourBytes & 0xffffff00) >> 8;
    pes_packet.stream_id = fourBytes & 0xff;

    /*
      MPTS spec - 2.4.3.7
      PES_packet_length  A 16-bit field specifying the number of bytes in the PES packet following the last byte of the field.
      A value of 0 indicates that the PES packet length is neither specified nor bounded and is allowed only in
      PES packets whose payload consists of bytes from a video elementary stream contained in Transport Stream packets.
    */

    pes_packet.PES_packet_length = util::read2Bytes(p);
    incPtr(p, 2);

    if (0 == pes_packet.PES_packet_length)
        pes_packet.PES_packet_length = PESPacketDataLength - 6;

    if (pes_packet.stream_id != program_stream_map &&
        pes_packet.stream_id != padding_stream &&
        pes_packet.stream_id != private_stream_2 &&
        pes_packet.stream_id != ECM_stream &&
        pes_packet.stream_id != EMM_stream &&
        pes_packet.stream_id != program_stream_directory &&
        pes_packet.stream_id != DSMCC_stream &&
        pes_packet.stream_id != itu_h222_e_stream)
    {
        uint8_t byte = *p;
        incPtr(p, 1);

        pes_packet.PES_scrambling_control = (byte & 0x30) >> 4;
        pes_packet.PES_priority = (byte & 0x08) >> 3;
        pes_packet.data_alignment_indicator = (byte & 0x04) >> 2;
        pes_packet.copyright = (byte & 0x02) >> 1;
        pes_packet.original_or_copy = byte & 0x01;

        byte = *p;
        incPtr(p, 1);

        pes_packet.PTS_DTS_flags = (byte & 0xC0) >> 6;
        pes_packet.ESCR_flag = (byte & 0x20) >> 5;
        pes_packet.ES_rate_flag = (byte & 0x10) >> 4;
        pes_packet.DSM_trick_mode_flag = (byte & 0x08) >> 3;
        pes_packet.additional_copy_info_flag = (byte & 0x04) >> 2;
        pes_packet.PES_CRC_flag = (byte & 0x02) >> 1;
        pes_packet.PES_extension_flag = byte & 0x01;

        /*
            PES_header_data_length  An 8-bit field specifying the total number of bytes occupied by the optional fields and any
            stuffing bytes contained in this PES packet header. The presence of optional fields is indicated in the byte that precedes
            the PES_header_data_length field.
        */
        uint8_t PES_header_data_length = *p;
        incPtr(p, 1);

        if (2 == pes_packet.PTS_DTS_flags)
        {
            pes_packet.PTS = readTimeStamp(p);
            pes_packet.DTS = pes_packet.PTS;

        }

        if (3 == pes_packet.PTS_DTS_flags)
        {
            pes_packet.PTS = readTimeStamp(p);
            pes_packet.DTS = readTimeStamp(p);
        }

        if (pes_packet.ESCR_flag) // 6 bytes
        {
            uint32_t byte = *p;
            incPtr(p, 1);

            // 31, 31, 30
            pes_packet.ESCR_base = (byte & 0x38) << 27;

            // 29, 28
            pes_packet.ESCR_base |= (byte & 0x03) << 29;

            byte = *p;
            incPtr(p, 1);

            // 27, 26, 25, 24, 23, 22, 21, 20
            pes_packet.ESCR_base |= byte << 19;

            byte = *p;
            incPtr(p, 1);

            // 19, 18, 17, 16, 15
            pes_packet.ESCR_base |= (byte & 0xF8) << 11;

            // 14, 13
            pes_packet.ESCR_base |= (byte & 0x03) << 13;

            byte = *p;
            incPtr(p, 1);

            // 12, 11, 10, 9, 8, 7, 6, 5
            pes_packet.ESCR_base |= byte << 4;

            byte = *p;
            incPtr(p, 1);

            // 4, 3, 2, 1, 0
            pes_packet.ESCR_base |= (byte & 0xF8) >> 3;

            pes_packet.ESCR_extension = (byte & 0x03) << 7;

            byte = *p;
            incPtr(p, 1);

            pes_packet.ESCR_extension |= (byte & 0xFE) >> 1;
        }

        if (pes_packet.ES_rate_flag)
        {
            uint32_t fourBytes = *p;
            incPtr(p, 1);
            fourBytes <<= 8;

            fourBytes |= *p;
            incPtr(p, 1);
            fourBytes <<= 8;

            fourBytes |= *p;
            incPtr(p, 1);
            fourBytes <<= 8;

            pes_packet.ES_rate = (fourBytes & 0x7FFFFE) >> 1;
        }

        if (pes_packet.DSM_trick_mode_flag)
        {
            // Table 2-24  Trick mode control values
            // Value Description
            // '000' Fast forward
            // '001' Slow motion
            // '010' Freeze frame
            // '011' Fast reverse
            // '100' Slow reverse
            // '101'-'111' Reserved

            uint8_t byte = *p;
            incPtr(p, 1);

            pes_packet.trick_mode_control = byte >> 5;

            if (0 == pes_packet.trick_mode_control) // Fast forward
            {
                pes_packet.field_id = (byte & 0x18) >> 3;
                pes_packet.intra_slice_refresh = (byte & 0x04) >> 2;
                pes_packet.frequency_truncation = byte & 0x03;
            }
            else if (1 == pes_packet.trick_mode_control) // Slow motion
            {
                pes_packet.rep_cntrl = byte & 0x1f;
            }
            else if (2 == pes_packet.trick_mode_control) // Freeze frame
            {
                pes_packet.field_id = (byte & 0x18) >> 3;
            }
            else if (3 == pes_packet.trick_mode_control) // Fast reverse
            {
                pes_packet.field_id = (byte & 0x18) >> 3;
                pes_packet.intra_slice_refresh = (byte & 0x04) >> 2;
                pes_packet.frequency_truncation = byte & 0x03;
            }
            else if (4 == pes_packet.trick_mode_control) // Slow reverse
            {
                pes_packet.rep_cntrl = byte & 0x1f;
            }
        }

        if (pes_packet.additional_copy_info_flag)
        {
            uint8_t byte = *p;
            incPtr(p, 1);

            pes_packet.additional_copy_info = byte & 0x7F;
        }

        if (pes_packet.PES_CRC_flag)
        {
            pes_packet.previous_PES_packet_CRC = util::read2Bytes(p);
            incPtr(p, 2);
        }

        if (pes_packet.PES_extension_flag)
        {
            uint8_t byte = *p;
            incPtr(p, 1);

            pes_packet.PES_private_data_flag = (byte & 0x80) >> 7;
            pes_packet.pack_header_field_flag = (byte & 0x40) >> 6;
            pes_packet.program_packet_sequence_counter_flag = (byte & 0x20) >> 5;
            pes_packet.P_STD_buffer_flag = (byte & 0x10) >> 4;
            // 3 bits Reserved
            pes_packet.PES_extension_flag_2 = byte & 0x01;

            if (pes_packet.PES_private_data_flag)
            {
                pes_packet.PES_private_data[16];
                std::memcpy(pes_packet.PES_private_data, p, 16);
                incPtr(p, 16);
            }

            if (pes_packet.pack_header_field_flag)
            {
                pes_packet.pack_field_length = *p;
                incPtr(p, 1);

                // pack_header is here
                // http://stnsoft.com/DVD/packhdr.html

                incPtr(p, pes_packet.pack_field_length);
            }

            if (pes_packet.program_packet_sequence_counter_flag)
            {
                uint8_t byte = *p;
                incPtr(p, 1);

                pes_packet.program_packet_sequence_counter = byte & 0x07F;

                byte = *p;
                incPtr(p, 1);

                pes_packet.MPEG1_MPEG2_identifier = (byte & 0x40) >> 6;
                pes_packet.original_stuff_length = byte & 0x3F;
            }

            if (pes_packet.P_STD_buffer_flag)
            {
                uint16_t two_bytes = util::read2Bytes(p);
                incPtr(p, 2);

                pes_packet.P_STD_buffer_scale = (two_bytes & 0x2000) >> 13;
                pes_packet.P_STD_buffer_size = two_bytes & 0x1FFF;
            }

            if (pes_packet.PES_extension_flag_2)
            {
                uint8_t byte = *p;
                incPtr(p, 1);

                pes_packet.PES_extension_field_length = byte & 0x7F;

                byte = *p;
                incPtr(p, 1);

                pes_packet.stream_id_extension_flag = (byte & 0x80) >> 7;

                if (0 == pes_packet.stream_id_extension_flag)
                {
                    pes_packet.stream_id_extension = byte & 0x7F;
                }
                else
                {
                    pes_packet.tref_extension_flag = byte & 0x1;
                    if (pes_packet.tref_extension_flag == 0)
                    {
                        byte = *p;
                        incPtr(p, 1);

                        // 32, 31, 30
                        pes_packet.TREF = (byte & 0x38) << 27;

                        // 29, 28
                        pes_packet.TREF |= (byte & 0x03) << 29;

                        byte = *p;
                        incPtr(p, 1);

                        // 27, 26, 25, 24, 23, 22, 21, 20
                        pes_packet.TREF |= byte << 19;

                        byte = *p;
                        incPtr(p, 1);

                        // 19, 18, 17, 16, 15
                        pes_packet.TREF |= (byte & 0xF8) << 11;

                        // 14, 13
                        pes_packet.TREF |= (byte & 0x03) << 13;

                        byte = *p;
                        incPtr(p, 1);

                        // 12, 11, 10, 9, 8, 7, 6, 5
                        pes_packet.TREF |= byte << 4;

                        byte = *p;
                        incPtr(p, 1);

                        // 4, 3, 2, 1, 0
                        pes_packet.TREF |= (byte & 0xF8) >> 3;
                    }
                }

                // NOTE – The value N3 equals the byte count given by PES_extension_field_length
                // minus the bytes which contain the stream_id_extension_flag and the occurring
                // data elements in the if-else-construct after the stream_id_extension_flag.
                incPtr(p, pes_packet.PES_extension_field_length); // Reserved
            }
        }

        /*
            From the TS spec:
            stuffing_byte  This is a fixed 8-bit value equal to '1111 1111' that can be inserted by the encoder, for example to meet
            the requirements of the channel. It is discarded by the decoder. No more than 32 stuffing bytes shall be present in one
            PES packet header.
        */

        while (*p == 0xFF)
            incPtr(p, 1);
    }
    else if (pes_packet.stream_id == program_stream_map ||
        pes_packet.stream_id == private_stream_2 ||
        pes_packet.stream_id == ECM_stream ||
        pes_packet.stream_id == EMM_stream ||
        pes_packet.stream_id == program_stream_directory ||
        pes_packet.stream_id == DSMCC_stream ||
        pes_packet.stream_id == itu_h222_e_stream)
    {
        // PES_packet_data here
        incPtr(p, pes_packet.PES_packet_length);
    }
    else if (pes_packet.stream_id == padding_stream)
    {
        // Padding bytes here
        incPtr(p, pes_packet.PES_packet_length);
    }

    return p - pStart;
}

// http://dvd.sourceforge.net/dvdinfo/pes-hdr.html
size_t mptsParser::processPESPacketHeader(uint8_t *&p, size_t PESPacketDataLength)
{
    uint8_t *pStart = p;

    uint32_t fourBytes = util::read4Bytes(p);
    incPtr(p, 4);

    uint32_t packet_start_code_prefix = (fourBytes & 0xffffff00) >> 8;
    uint8_t stream_id = fourBytes & 0xff;

    /* MPTS spec - 2.4.3.7
      PES_packet_length  A 16-bit field specifying the number of bytes in the PES packet following the last byte of the field.
      A value of 0 indicates that the PES packet length is neither specified nor bounded and is allowed only in
      PES packets whose payload consists of bytes from a video elementary stream contained in Transport Stream packets.
    */

    int64_t PES_packet_length = util::read2Bytes(p);
    incPtr(p, 2);

    if (0 == PES_packet_length)
        PES_packet_length = PESPacketDataLength - 6;

    if (stream_id != program_stream_map &&
        stream_id != padding_stream &&
        stream_id != private_stream_2 &&
        stream_id != ECM_stream &&
        stream_id != EMM_stream &&
        stream_id != program_stream_directory &&
        stream_id != DSMCC_stream &&
        stream_id != itu_h222_e_stream)
    {
        uint8_t byte = *p;
        incPtr(p, 1);

        uint8_t PES_scrambling_control = (byte & 0x30) >> 4;
        uint8_t PES_priority = (byte & 0x08) >> 3;
        uint8_t data_alignment_indicator = (byte & 0x04) >> 2;
        uint8_t copyright = (byte & 0x02) >> 1;
        uint8_t original_or_copy = byte & 0x01;

        byte = *p;
        incPtr(p, 1);

        uint8_t PTS_DTS_flags = (byte & 0xC0) >> 6;
        uint8_t ESCR_flag = (byte & 0x20) >> 5;
        uint8_t ES_rate_flag = (byte & 0x10) >> 4;
        uint8_t DSM_trick_mode_flag = (byte & 0x08) >> 3;
        uint8_t additional_copy_info_flag = (byte & 0x04) >> 2;
        uint8_t PES_CRC_flag = (byte & 0x02) >> 1;
        uint8_t PES_extension_flag = byte & 0x01;

        /*
            PES_header_data_length  An 8-bit field specifying the total number of bytes occupied by the optional fields and any
            stuffing bytes contained in this PES packet header. The presence of optional fields is indicated in the byte that precedes
            the PES_header_data_length field.
        */
        uint8_t PES_header_data_length = *p;
        incPtr(p, 1);

        static uint64_t PTS_last = 0;

        if(2 == PTS_DTS_flags)
        {
            uint64_t PTS = readTimeStamp(p);
            printfXml(2, "<DTS>%llu (%f)</DTS>\n", PTS, convertTimeStamp(PTS));
            printfXml(2, "<PTS>%llu (%f)</PTS>\n", PTS, convertTimeStamp(PTS));
        }

        static uint64_t DTS_last = 0;

        if(3 == PTS_DTS_flags)
        {
            uint64_t PTS = readTimeStamp(p);
            uint64_t DTS = readTimeStamp(p);

            printfXml(2, "<DTS>%llu (%f)</DTS>\n", DTS, convertTimeStamp(DTS));
            printfXml(2, "<PTS>%llu (%f)</PTS>\n", PTS, convertTimeStamp(PTS));
        }

        if(ESCR_flag) // 6 bytes
        {
            uint32_t byte = *p;
            incPtr(p, 1);

            // 31, 31, 30
            uint32_t ESCR_base = (byte & 0x38) << 27;

            // 29, 28
            ESCR_base |= (byte & 0x03) << 29;

            byte = *p;
            incPtr(p, 1);

            // 27, 26, 25, 24, 23, 22, 21, 20
            ESCR_base |= byte << 19;

            byte = *p;
            incPtr(p, 1);

            // 19, 18, 17, 16, 15
            ESCR_base |= (byte & 0xF8) << 11;

            // 14, 13
            ESCR_base |= (byte & 0x03) << 13;

            byte = *p;
            incPtr(p, 1);

            // 12, 11, 10, 9, 8, 7, 6, 5
            ESCR_base |= byte << 4;

            byte = *p;
            incPtr(p, 1);

            // 4, 3, 2, 1, 0
            ESCR_base |= (byte & 0xF8) >> 3;

            uint32_t ESCR_extension = (byte & 0x03) << 7;

            byte = *p;
            incPtr(p, 1);

            ESCR_extension |= (byte & 0xFE) >> 1;
        }

        if(ES_rate_flag)
        {
            uint32_t fourBytes = *p;
            incPtr(p, 1);
            fourBytes <<= 8;

            fourBytes |= *p;
            incPtr(p, 1);
            fourBytes <<= 8;

            fourBytes |= *p;
            incPtr(p, 1);
            fourBytes <<= 8;

            uint32_t ES_rate = (fourBytes & 0x7FFFFE) >> 1;
        }

        if(DSM_trick_mode_flag)
        {
            // Table 2-24  Trick mode control values
            // Value Description
            // '000' Fast forward
            // '001' Slow motion
            // '010' Freeze frame
            // '011' Fast reverse
            // '100' Slow reverse
            // '101'-'111' Reserved

            uint8_t byte = *p;
            incPtr(p, 1);

            uint8_t trick_mode_control = byte >> 5;

            if(0 == trick_mode_control) // Fast forward
            {
                uint8_t field_id = (byte & 0x18) >> 3;
                uint8_t intra_slice_refresh = (byte & 0x04) >> 2;
                uint8_t frequency_truncation = byte & 0x03;
            }
            else if(1 == trick_mode_control) // Slow motion
            {
                uint8_t rep_cntrl = byte & 0x1f;
            }
            else if(2 == trick_mode_control) // Freeze frame
            {
                uint8_t field_id = (byte & 0x18) >> 3;
            }
            else if(3 == trick_mode_control) // Fast reverse
            {
                uint8_t field_id = (byte & 0x18) >> 3;
                uint8_t intra_slice_refresh = (byte & 0x04) >> 2;
                uint8_t frequency_truncation = byte & 0x03;
            }
            else if(4 == trick_mode_control) // Slow reverse
            {
                uint8_t rep_cntrl = byte & 0x1f;
            }
        }

        if(additional_copy_info_flag)
        {
            uint8_t byte = *p;
            incPtr(p, 1);

            uint8_t additional_copy_info = byte & 0x7F;
        }

        if(PES_CRC_flag)
        {
            uint16_t previous_PES_packet_CRC = util::read2Bytes(p);
            incPtr(p, 2);
        }

        if(PES_extension_flag)
        {
            uint8_t byte = *p;
            incPtr(p, 1);

            uint8_t PES_private_data_flag = (byte & 0x80) >> 7;
            uint8_t pack_header_field_flag = (byte & 0x40) >> 6;
            uint8_t program_packet_sequence_counter_flag = (byte & 0x20) >> 5;
            uint8_t P_STD_buffer_flag = (byte & 0x10) >> 4;
            // 3 bits Reserved
            uint8_t PES_extension_flag_2 = byte & 0x01;

            if(PES_private_data_flag)
            {
                uint8_t PES_private_data[16];
                std::memcpy(PES_private_data, p, 16);
                incPtr(p, 16);
            }

            if(pack_header_field_flag)
            {
                uint8_t pack_field_length = *p;
                incPtr(p, 1);

                // pack_header is here
                // http://stnsoft.com/DVD/packhdr.html

                incPtr(p, pack_field_length);
            }

            if(program_packet_sequence_counter_flag)
            {
                uint8_t byte = *p;
                incPtr(p, 1);

                uint8_t program_packet_sequence_counter = byte & 0x07F;

                byte = *p;
                incPtr(p, 1);

                uint8_t MPEG1_MPEG2_identifier = (byte & 0x40) >> 6;
                uint8_t original_stuff_length = byte & 0x3F;
            }

            if(P_STD_buffer_flag)
            {
                uint16_t two_bytes = util::read2Bytes(p);
                incPtr(p, 2);

                uint8_t P_STD_buffer_scale = (two_bytes & 0x2000) >> 13;
                uint8_t P_STD_buffer_size = two_bytes & 0x1FFF;
            }

            if(PES_extension_flag_2)
            {
                uint8_t byte = *p;
                incPtr(p, 1);

                uint8_t PES_extension_field_length = byte & 0x7F;

                byte = *p;
                incPtr(p, 1);

                uint8_t stream_id_extension_flag = (byte & 0x80) >> 7;

                if(0 == stream_id_extension_flag)
                {
                    uint8_t stream_id_extension = byte & 0x7F;
                }
                else
                {
                    uint8_t tref_extension_flag = byte & 0x1;
                    if (tref_extension_flag == 0)
                    {
                        byte = *p;
                        incPtr(p, 1);

                        // 32, 31, 30
                        uint32_t TREF = (byte & 0x38) << 27;

                        // 29, 28
                        TREF |= (byte & 0x03) << 29;

                        byte = *p;
                        incPtr(p, 1);

                        // 27, 26, 25, 24, 23, 22, 21, 20
                        TREF |= byte << 19;

                        byte = *p;
                        incPtr(p, 1);

                        // 19, 18, 17, 16, 15
                        TREF |= (byte & 0xF8) << 11;

                        // 14, 13
                        TREF |= (byte & 0x03) << 13;

                        byte = *p;
                        incPtr(p, 1);

                        // 12, 11, 10, 9, 8, 7, 6, 5
                        TREF |= byte << 4;

                        byte = *p;
                        incPtr(p, 1);

                        // 4, 3, 2, 1, 0
                        TREF |= (byte & 0xF8) >> 3;
                    }
                }
 
                // NOTE – The value N3 equals the byte count given by PES_extension_field_length
                // minus the bytes which contain the stream_id_extension_flag and the occurring
                // data elements in the if-else-construct after the stream_id_extension_flag.
                incPtr(p, PES_extension_field_length); // Reserved
            }
        }

        /*
            From the TS spec:
            stuffing_byte  This is a fixed 8-bit value equal to '1111 1111' that can be inserted by the encoder, for example to meet
            the requirements of the channel. It is discarded by the decoder. No more than 32 stuffing bytes shall be present in one
            PES packet header.
        */

        while(*p == 0xFF)
            incPtr(p,1);
    }
    else if (stream_id == program_stream_map ||
             stream_id == private_stream_2 ||
             stream_id == ECM_stream ||
             stream_id == EMM_stream ||
             stream_id == program_stream_directory ||
             stream_id == DSMCC_stream ||
             stream_id == itu_h222_e_stream)
    {
        // PES_packet_data here
        incPtr(p, PES_packet_length);
    }
    else if (stream_id == padding_stream)
    {
        // Padding bytes here
        incPtr(p, PES_packet_length);
    }

    return p - pStart;
}

// Push data into video buffer for later processing by a decoder
size_t mptsParser::processPESPacket(uint8_t *&packetStart, uint8_t *&p, eMptsStreamType streamType, bool payloadUnitStart)
{
#if 1
    size_t PESPacketDataLength = m_packetSize - (p - packetStart);

    if (m_bAnalyzeElementaryStream)
        pushVideoData(p, PESPacketDataLength);

    incPtr(p, PESPacketDataLength);
    return PESPacketDataLength;
#else
    ////
    // For fuller parsing enable this block, even though at this point it does not do much
    ////
    if(false == payloadUnitStart)
    {
        size_t PESPacketDataLength = m_packetSize - (p - packetStart);
        
        if(m_bAnalyzeElementaryStream)
            pushVideoData(p, PESPacketDataLength);

        incPtr(p, PESPacketDataLength);
        return PESPacketDataLength;
    }

    // Peek at the next 6 bytes to figure out stream_id
    uint32_t fourBytes = util::read4Bytes(p);

    uint32_t packet_start_code_prefix = (fourBytes & 0xffffff00) >> 8;
    uint8_t stream_id = fourBytes & 0xff;

    /* 2.4.3.7
      PES_packet_length  A 16-bit field specifying the number of bytes in the PES packet following the last byte of the field.
      A value of 0 indicates that the PES packet length is neither specified nor bounded and is allowed only in
      PES packets whose payload consists of bytes from a video elementary stream contained in Transport Stream packets.
    */

    int64_t PES_packet_length = util::read2Bytes(p+4);

    if(0 == PES_packet_length)
        PES_packet_length = m_packetSize - (p - packetStart);

    // PES_packet_length is not needed in this version of the code
    // We want to limit our data copies to just the 192 or 188 packet length
    size_t PESPacketDataLength = m_packetSize - (p - packetStart);

    if (stream_id != program_streamMap &&
        stream_id != padding_stream &&
        stream_id != private_stream_2 &&
        stream_id != ECM_stream &&
        stream_id != EMM_stream &&
        stream_id != program_stream_directory &&
        stream_id != DSMCC_stream &&
        stream_id != itu_h222_e_stream)
    {
        //if(eMPEG2_Video == stream_type ||
        //   eH264_Video == stream_type)
        //{
            // Push first PES packet, lots of info here.
            if(m_bAnalyzeElementaryStream)
                pushVideoData(p, PESPacketDataLength);

            incPtr(p, PESPacketDataLength);
            //}
        //else
        //{
        //    incPtr(p, PES_packet_length);
        //}
    }
    else if (stream_id == program_streamMap ||
             stream_id == private_stream_2 ||
             stream_id == ECM_stream ||
             stream_id == EMM_stream ||
             stream_id == program_stream_directory ||
             stream_id == DSMCC_stream ||
             stream_id == itu_h222_e_stream)
    {
        // PES_packet_data here
        incPtr(p, PESPacketDataLength);
    }
    else if (stream_id == padding_stream)
    {
        // Padding bytes here
        incPtr(p, PESPacketDataLength);
    }

    return PESPacketDataLength;
#endif
}

void mptsParser::printFrameInfo(mpts_frame *pFrame)
{
    if(pFrame)
    {
        if(pFrame->pidList.size())
        {
            for(mptsPidListType::size_type i = 0; i != pFrame->pidList.size(); i++)
                pFrame->totalPackets += pFrame->pidList[i].numPackets;

            if(m_bAnalyzeElementaryStream)
            {
                unsigned int framesReceived = 0;
                size_t bytesProcessed = processVideoFrames(m_pVideoData, m_videoDataSize, pFrame);
                //compact_video_data(bytesProcessed);
                popVideoData();
            }

            pFrame->totalPackets = 0;
        }
    }
}

// Process each PID (Packet Identifier) for each 188 byte packet
// https://en.wikipedia.org/wiki/MPEG_transport_stream#Packet_identifier_(PID)
// https://www.linuxtv.org/wiki/index.php/PID
/*
Packet identifiers in use
Decimal	    Hexadecimal	    Description
0	        0x0000	        Program association table (PAT) contains a directory listing of all program map tables
1	        0x0001	        Conditional access table (CAT) contains a directory listing of all ITU-T Rec. H.222 entitlement management message streams used by program map tables
2	        0x0002	        Transport stream description table (TSDT) contains descriptors relating to the overall transport stream
3	        0x0003	        IPMP control information table contains a directory listing of all ISO/IEC 14496-13 control streams used by program map tables
415	    0x0004-0x000F	Reserved for future use
-----------------------
1631	    0x0010-0x001F	Used by DVB metadata[10]
            0x0010: NIT, ST
            0x0011: SDT, BAT, ST
            0x0012: EIT, ST, CIT
            0x0013: RST, ST
            0x0014: TDT, TOT, ST
            0x0015: network synchronization
            0x0016: RNT
            0x0017-0x001B: reserved for future use
            0x001C: inband signalling
            0x001D: measurement
            0x001E: DIT
            0x001F: SIT
-----------------------
32-8186	    0x0020-0x1FFA	May be assigned as needed to program map tables, elementary streams and other data tables
8187	    0x1FFB	Used by DigiCipher 2/ATSC MGT metadata
8188-8190	0x1FFC-0x1FFE	May be assigned as needed to program map tables, elementary streams and other data tables
8191	    0x1FFF	        Null Packet (used for fixed bandwidth padding)
*/

int16_t mptsParser::processPid(uint16_t pid, uint8_t *&packetStart, uint8_t *&p, int64_t packetStartInFile, size_t packetNum, bool payloadUnitStart, uint8_t adaptationFieldLength)
{
    static size_t lastPid = -1;

    if(ePAT == pid) // PAT - Program Association Table
    {
        if(m_bTerse)
        {
            printfXml(1, "<packet start=\"%llu\">\n", packetStartInFile);
            printfXml(2, "<number>%zd</number>\n", packetNum);
            printfXml(2, "<pid>0x%x</pid>\n", pid);
            printfXml(2, "<payload_unit_start_indicator>0x%x</payload_unit_start_indicator>\n", payloadUnitStart ? 1 : 0);
        }

        program_association_table pat;
        readPAT(p, pat, payloadUnitStart);

        printfXml(2, "<program_association_table>\n");
        if (pat.payload_unit_start)
            printfXml(3, "<pointer_field>0x%x</pointer_field>\n", pat.payload_start_offset);
        printfXml(3, "<table_id>0x%x</table_id>\n", pat.table_id);
        printfXml(3, "<section_syntax_indicator>%d</section_syntax_indicator>\n", pat.section_syntax_indicator);
        printfXml(3, "<section_length>%d</section_length>\n", pat.section_length);
        printfXml(3, "<transport_stream_id>0x%x</transport_stream_id>\n", pat.transport_stream_id);
        printfXml(3, "<version_number>0x%x</version_number>\n", pat.version_number);
        printfXml(3, "<current_next_indicator>0x%x</current_next_indicator>\n", pat.current_next_indicator);
        printfXml(3, "<section_number>0x%x</section_number>\n", pat.section_number);
        printfXml(3, "<last_section_number>0x%x</last_section_number>\n", pat.last_section_number);

        for (const auto [program_number, pid] : pat.program_numbers)
        {
            printfXml(3, "<program>\n");
            printfXml(4, "<number>%d</number>\n", program_number);

            if (0 == program_number)
            {
                printfXml(4, "<network_pid>0x%x</network_pid>\n", pid);
                m_networkPid = pid;
            }
            else
            {
                printfXml(4, "<program_map_pid>0x%x</program_map_pid>\n", pid);
                m_programMapPid = pid;
            }

            printfXml(3, "</program>\n");
        }

        printfXml(2, "</program_association_table>\n");

        if(m_bTerse)
            printfXml(1, "</packet>\n");
    }
    else if(m_programMapPid == pid)
    {
        if(m_bTerse)
        {
            printfXml(1, "<packet start=\"%llu\">\n", packetStartInFile);
            printfXml(2, "<number>%zd</number>\n", packetNum);
            printfXml(2, "<pid>0x%x</pid>\n", pid);
            printfXml(2, "<payload_unit_start_indicator>0x%x</payload_unit_start_indicator>\n", payloadUnitStart ? 1 : 0);
        }

        program_map_table pmt;
        readPMT(p, pmt, payloadUnitStart);

        // This has to be done by hand
        m_pidToNameMap[0x1FFF] = "NULL Packet";
        m_pidToNameMap[pmt.pcr_pid] = "PCR";

        printfXml(2, "<program_map_table>\n");
        if (pmt.payload_unit_start)
            printfXml(3, "<pointer_field>0x%x</pointer_field>\n", pmt.payload_start_offset);
        printfXml(3, "<table_id>0x%x</table_id>\n", pmt.table_id);
        printfXml(3, "<section_syntax_indicator>%d</section_syntax_indicator>\n", pmt.section_syntax_indicator);
        printfXml(3, "<section_length>%d</section_length>\n", pmt.section_length);
        printfXml(3, "<program_number>%d</program_number>\n", pmt.program_number);
        printfXml(3, "<version_number>%d</version_number>\n", pmt.version_number);
        printfXml(3, "<current_next_indicator>%d</current_next_indicator>\n", pmt.current_next_indicator);
        printfXml(3, "<section_number>%d</section_number>\n", pmt.section_number);
        printfXml(3, "<last_section_number>%d</last_section_number>\n", pmt.last_section_number);
        printfXml(3, "<pcr_pid>0x%x</pcr_pid>\n", pmt.pcr_pid);
        printfXml(3, "<program_info_length>%d</program_info_length>\n", pmt.program_info_length);

        printElementDescriptors(pmt);

        std::map <uint16_t, char*> streamMap; // ID, name
        initStreamTypes(streamMap);
        size_t stream_count = 0;

        for (const auto [stream_type, elementary_pid, es_info_length] : pmt.program_elements)
        {
            // Scte35 stream type is 0x86
            if (0x86 == stream_type)
                m_scte35Pid = elementary_pid;

            m_pidToNameMap[elementary_pid] = streamMap[stream_type];
            m_pidToTypeMap[elementary_pid] = (eMptsStreamType)stream_type;

            printfXml(3, "<stream>\n");
            printfXml(4, "<number>%zd</number>\n", stream_count);
            printfXml(4, "<pid>0x%x</pid>\n", elementary_pid);
            printfXml(4, "<type_number>0x%x</type_number>\n", stream_type);
            printfXml(4, "<type_name>%s</type_name>\n", streamMap[stream_type]);
            printfXml(3, "</stream>\n");

            stream_count++;
        }

        printfXml(2, "</program_map_table>\n");

        if(m_bTerse)
            printfXml(1, "</packet>\n");
    }
    else if(pid >= eAsNeededStart && pid <= eAsNeededEnd)
    {
        if(false == m_bTerse)
        {
            // Here, p is pointing at actual data, like video or audio.
            // For now just print the data's type.
            printfXml(2, "<type_name>%s</type_name>\n", m_pidToNameMap[pid]);
        }
        else
        {
            mpts_frame *p_frame = nullptr;

            switch(m_pidToTypeMap[pid])
            {
                case eMPEG2_Video:
                    if(nullptr == m_parser)
                        m_parser = std::shared_ptr<baseParser>(new mpeg2Parser());

                    p_frame = &m_videoFrame;
                    p_frame->pid = pid;
                    p_frame->streamType = eMPEG2_Video;
                break;
                case eH264_Video:
                    if(nullptr == m_parser)
                        m_parser = std::shared_ptr<baseParser>(new avcParser());

                    p_frame = &m_videoFrame;
                    p_frame->pid = pid;
                    p_frame->streamType = eH264_Video;
                break;
                case eMPEG1_Video:
                case eMPEG4_Video:
                case eDigiCipher_II_Video:
                case eMSCODEC_Video:
                break;

                case eMPEG1_Audio:
                case eMPEG2_Audio:
                case eMPEG2_AAC_Audio:
                case eMPEG4_LATM_AAC_Audio:
                case eA52_AC3_Audio:
                case eHDMV_DTS_Audio:
                case eA52b_AC3_Audio:
                case eSDDS_Audio:
                    //p_frame = &m_audioFrame;
                    //p_frame->pid = pid;
                break;
            }

            if(p_frame)
            {
                bool bNewSet = false;

                if(payloadUnitStart)
                {
                    // When we get the start of a new payload decode and gather information about the previous payload
                    printFrameInfo(p_frame);

                    p_frame->pidList.clear();
                    bNewSet = true;
                }

                if(-1 != lastPid && pid != lastPid)
                    bNewSet = true;

                if(bNewSet)
                {
                    mptsPidEntryType pet(m_pidToNameMap[pid], 1, packetStartInFile);
                    p_frame->pidList.push_back(pet);
                }
                else
                {
                    mptsPidEntryType &pet = p_frame->pidList.back();
                    pet.numPackets++;
                }

                p += adaptationFieldLength;

                if(p - packetStart != m_packetSize)
                    processPESPacket(packetStart, p, m_pidToTypeMap[pid], payloadUnitStart);
            }
        }
    }

    lastPid = pid;

    return 0;
}

uint8_t mptsParser::getAdaptationFieldLength(uint8_t *&p)
{
    uint8_t adaptation_field_length = *p;
    return adaptation_field_length + 1;
}

uint8_t mptsParser::processAdaptationField(unsigned int indent, uint8_t *&p)
{
    uint8_t adaptation_field_length = *p;
    incPtr(p, 1);

    uint8_t *pAdapatationFieldStart = p;

    if(adaptation_field_length)
    {
        uint8_t byte = *p;
        incPtr(p, 1);

        uint8_t discontinuity_indicator = (byte & 0x80) >> 7;
        uint8_t random_access_indicator = (byte & 0x40) >> 6;
        uint8_t elementary_stream_priority_indicato = (byte & 0x20) >> 5;
        uint8_t PCR_flag = (byte & 0x10) >> 4;
        uint8_t OPCR_flag = (byte & 0x08) >> 3;
        uint8_t splicing_point_flag = (byte & 0x04) >> 2;
        uint8_t transport_private_data_flag = (byte & 0x02) >> 1;
        uint8_t adaptation_field_extension_flag = (byte & 0x02) >> 1;

        if(PCR_flag)
        {
            uint32_t fourBytes = util::read4Bytes(p);
            incPtr(p, 4);

            uint16_t two_bytes = util::read2Bytes(p);
            incPtr(p, 2);

            uint64_t program_clock_reference_base = fourBytes;
            program_clock_reference_base <<= 1;
            program_clock_reference_base |= (two_bytes & 0x80) >> 7;

            uint16_t program_clock_reference_extension = two_bytes & 0x1ff;
        }

        if(OPCR_flag)
        {
            uint32_t fourBytes = util::read4Bytes(p);
            incPtr(p, 4);

            uint16_t two_bytes = util::read2Bytes(p);
            incPtr(p, 2);

            uint64_t original_program_clock_reference_base = fourBytes;
            original_program_clock_reference_base <<= 1;
            original_program_clock_reference_base |= (two_bytes & 0x80) >> 7;

            uint16_t original_program_clock_reference_extension = two_bytes & 0x1ff;
        }

        if(splicing_point_flag)
        {
            uint8_t splice_countdown = *p;
            incPtr(p, 1);
        }

        if(transport_private_data_flag)
        {
            uint8_t transport_private_data_length = *p;
            incPtr(p, 1);

            for(unsigned int i = 0; i < transport_private_data_length; i++)
                p++;
        }

        if(adaptation_field_extension_flag)
        {
            size_t adaptation_field_extension_length = *p;
            incPtr(p, 1);

            uint8_t *pAdapatationFieldExtensionStart = p;

            uint8_t byte = *p;
            incPtr(p, 1);

            uint8_t ltw_flag = (byte & 0x80) >> 7;
            uint8_t piecewise_rate_flag = (byte & 0x40) >> 6;
            uint8_t seamless_splice_flag = (byte & 0x20) >> 5;

            if(ltw_flag)
            {
                uint16_t two_bytes = util::read2Bytes(p);
                incPtr(p, 2);

                uint8_t ltw_valid_flag = (two_bytes & 0x8000) >> 15;
                uint16_t ltw_offset = two_bytes & 0x7fff;
            }

            if(piecewise_rate_flag)
            {
                uint16_t two_bytes = util::read2Bytes(p);
                incPtr(p, 2);

                uint32_t piecewise_rate = two_bytes & 0x3fffff;
            }

            if(seamless_splice_flag)
            {
                uint32_t byte = *p;
                incPtr(p, 1);

                uint32_t DTS_next_AU;
                uint8_t splice_type = (byte & 0xf0) >> 4;
                DTS_next_AU = (byte & 0xe) << 28;

                uint32_t two_bytes = util::read2Bytes(p);
                incPtr(p, 2);

                DTS_next_AU |= (two_bytes & 0xfe) << 13;

                two_bytes = util::read2Bytes(p);
                incPtr(p, 2);

                DTS_next_AU |= (two_bytes & 0xfe) >> 1;
            }

            size_t N = adaptation_field_extension_length - (p - pAdapatationFieldExtensionStart);
            for(unsigned int i = 0; i < N; i++)
                p++; // reserved
        }

        size_t N = adaptation_field_length - (p - pAdapatationFieldStart);
        for(unsigned int i = 0; i < N; i++)
            p++; // stuffing_byte
    }

    return adaptation_field_length;
}

// Get the PID and other info
int16_t mptsParser::processPacket(uint8_t *packet, size_t packetNum)
{
    uint8_t *p = NULL;
    int16_t ret = 0;
    int64_t packetStartInFile = m_filePosition;

    if(false == m_bTerse)
    {
        printfXml(1, "<packet start=\"%llu\">\n", m_filePosition);
        printfXml(2, "<number>%zd</number>\n", packetNum);
    }

    p = packet;

    if (SYNC_BYTE != *p)
    {
        printfXml(2, "<error>Packet %zd does not start with 0x47</error>\n", packetNum);
        fprintf(stderr, "Error: Packet %zd does not start with 0x47\n", packetNum);
        goto process_packet_error;
    }

    // Skip the sync byte 0x47
    incPtr(p, 1);

    uint16_t pid = util::read2Bytes(p);
    incPtr(p, 2);

    uint8_t transport_error_indicator = (pid & 0x8000) >> 15;
    uint8_t payload_unit_start_indicator = (pid & 0x4000) >> 14;

    uint8_t transport_priority = (pid & 0x2000) >> 13;

    pid &= 0x1FFF;

    // Move beyond the 32 bit header
    uint8_t final_byte = *p;
    incPtr(p, 1);

    uint8_t transport_scrambling_control = (final_byte & 0xC0) >> 6;
    uint8_t adaptation_field_control = (final_byte & 0x30) >> 4;
    uint8_t continuity_counter = (final_byte & 0x0F);

    if(false == m_bTerse)
    {
        printfXml(2, "<pid>0x%x</pid>\n", pid);
        printfXml(2, "<payload_unit_start_indicator>0x%x</payload_unit_start_indicator>\n", payload_unit_start_indicator);
        printfXml(2, "<transport_error_indicator>0x%x</transport_error_indicator>\n", transport_error_indicator);
        printfXml(2, "<transport_priority>0x%x</transport_priority>\n", transport_priority);
        printfXml(2, "<transport_scrambling_control>0x%x</transport_scrambling_control>\n", transport_scrambling_control);
        printfXml(2, "<adaptation_field_control>0x%x</adaptation_field_control>\n", adaptation_field_control);
        printfXml(2, "<continuity_counter>0x%x</continuity_counter>\n", continuity_counter);
    }

    /*
        Table 2-5  Adaptation field control values
            Value  Description
             00    Reserved for future use by ISO/IEC
             01    No adaptation_field, payload only
             10    Adaptation_field only, no payload
             11    Adaptation_field followed by payload
    */
    uint8_t adaptation_field_length = 0;

    if(2 == adaptation_field_control)
        adaptation_field_length = m_packetSize - 4;
    else if(3 == adaptation_field_control)
        adaptation_field_length = getAdaptationFieldLength(p);

    ret = processPid(pid, packet, p, packetStartInFile, packetNum, 1 == payload_unit_start_indicator, adaptation_field_length);

process_packet_error:

    if(false == m_bTerse)
        printfXml(1, "</packet>\n");

    return ret;
}

// 2.4.3.6 PES Packet
//
// Return a 33 bit number representing the time stamp
uint64_t mptsParser::readTimeStamp(uint8_t *&p)
{
    uint64_t byte = *p;
    incPtr(p, 1);

    uint64_t time_stamp = (byte & 0x0E) << 29;

    uint64_t two_bytes = util::read2Bytes(p);
    incPtr(p, 2);

    time_stamp |= (two_bytes & 0xFFFE) << 14;

    two_bytes = util::read2Bytes(p);
    incPtr(p, 2);

    time_stamp |= (two_bytes & 0xFFFE) >> 1;

    return time_stamp;
}

float mptsParser::convertTimeStamp(uint64_t time_stamp)
{
    return (float) time_stamp / 90000.f;
}

void printSpsData(const SequenceParameterSet& sps)
{
    util::printfXml(1, "<SPS>\n");

    util::printfXml(2, "<profile_idc>%d</profile_idc>\n", sps.profile_idc);
    util::printfXml(2, "<constraint_set0_flag>%d</constraint_set0_flag>\n", sps.constraint_set0_flag);
    util::printfXml(2, "<constraint_set1_flag>%d</constraint_set1_flag>\n", sps.constraint_set1_flag);
    util::printfXml(2, "<constraint_set2_flag>%d</constraint_set2_flag>\n", sps.constraint_set2_flag);
    util::printfXml(2, "<constraint_set3_flag>%d</constraint_set3_flag>\n", sps.constraint_set3_flag);
    util::printfXml(2, "<constraint_set4_flag>%d</constraint_set4_flag>\n", sps.constraint_set4_flag);
    util::printfXml(2, "<constraint_set5_flag>%d</constraint_set5_flag>\n", sps.constraint_set5_flag);
    util::printfXml(2, "<level_idc>%d</level_idc>\n", sps.level_idc);
    util::printfXml(2, "<seq_parameter_set_id>%d</seq_parameter_set_id>\n", sps.seq_parameter_set_id);

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
        util::printfXml(2, "<chroma_format_idc>%d</chroma_format_idc>\n", sps.chroma_format_idc);

        if (3 == sps.chroma_format_idc)
        {
            util::printfXml(4, "<separate_colour_plane_flag>%d</separate_colour_plane_flag>\n", sps.separate_colour_plane_flag);
        }

        util::printfXml(2, "<bit_depth_luma_minus8>%d</bit_depth_luma_minus8>\n", sps.bit_depth_luma_minus8);
        util::printfXml(2, "<bit_depth_chroma_minus8>%d</bit_depth_chroma_minus8>\n", sps.bit_depth_chroma_minus8);
        util::printfXml(2, "<qpprime_y_zero_transform_bypass_flag>%d</qpprime_y_zero_transform_bypass_flag>\n", sps.qpprime_y_zero_transform_bypass_flag);
        util::printfXml(2, "<seq_scaling_matrix_present_flag>%d</seq_scaling_matrix_present_flag>\n", sps.seq_scaling_matrix_present_flag);

        if (sps.seq_scaling_matrix_present_flag)
        {
            for (int i = 0; i < sps.seq_scaling_list_present_flag.size(); i++)
            {
                util::printfXml(3, "<seq_scaling_list_present_flag[%d]>%d</seq_scaling_list_present_flag>\n", i, sps.seq_scaling_list_present_flag[i]);

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

    util::printfXml(2, "<log2_max_frame_num_minus4>%d</log2_max_frame_num_minus4>\n", sps.log2_max_frame_num_minus4);
    util::printfXml(2, "<pic_order_cnt_type>%d</pic_order_cnt_type>\n", sps.pic_order_cnt_type);

    if (0 == sps.pic_order_cnt_type)
    {
        util::printfXml(2, "<log2_max_pic_order_cnt_lsb_minus4>%d</log2_max_pic_order_cnt_lsb_minus4>\n", sps.log2_max_pic_order_cnt_lsb_minus4);
    }
    else if (1 == sps.pic_order_cnt_type)
    {
        util::printfXml(2, "<delta_pic_order_always_zero_flag>%d</delta_pic_order_always_zero_flag>\n", sps.delta_pic_order_always_zero_flag);
        util::printfXml(2, "<offset_for_non_ref_pic>%d</offset_for_non_ref_pic>\n", sps.offset_for_non_ref_pic);
        util::printfXml(2, "<offset_for_top_to_bottom_field>%d</offset_for_top_to_bottom_field>\n", sps.offset_for_top_to_bottom_field);
        util::printfXml(2, "<num_ref_frames_in_pic_order_cnt_cycle>%d</num_ref_frames_in_pic_order_cnt_cycle>\n", sps.num_ref_frames_in_pic_order_cnt_cycle);

        for (int i = 0; i < sps.num_ref_frames_in_pic_order_cnt_cycle; i++)
        {
            // TODO: Create an array
            util::printfXml(3, "<offset_for_ref_frame[%d]>%d</offset_for_ref_frame>\n", i, sps.offset_for_ref_frame);
        }
    }

    util::printfXml(2, "<max_num_ref_frames>%d</max_num_ref_frames>\n", sps.max_num_ref_frames);
    util::printfXml(2, "<gaps_in_frame_num_value_allowed_flag>%d</gaps_in_frame_num_value_allowed_flag>\n", sps.gaps_in_frame_num_value_allowed_flag);
    util::printfXml(2, "<pic_width_in_mbs_minus1>%d</pic_width_in_mbs_minus1>\n", sps.pic_width_in_mbs_minus1);
    util::printfXml(2, "<pic_height_in_map_units_minus1>%d</pic_height_in_map_units_minus1>\n", sps.pic_height_in_map_units_minus1);
    util::printfXml(2, "<frame_mbs_only_flag>%d</frame_mbs_only_flag>\n", sps.frame_mbs_only_flag);

    if (0 == sps.frame_mbs_only_flag)
    {
        util::printfXml(3, "<mb_adaptive_frame_field_flag>%d</mb_adaptive_frame_field_flag>\n", sps.mb_adaptive_frame_field_flag);
    }

    util::printfXml(2, "<direct_8x8_inference_flag>%d</direct_8x8_inference_flag>\n", sps.direct_8x8_inference_flag);
    util::printfXml(2, "<frame_cropping_flag>%d</frame_cropping_flag>\n", sps.frame_cropping_flag);

    if (sps.frame_cropping_flag)
    {
        util::printfXml(3, "<frame_crop_left_offset>%d</frame_crop_left_offset>\n", sps.frame_crop_left_offset);
        util::printfXml(3, "<frame_crop_right_offset>%d</frame_crop_right_offset>\n", sps.frame_crop_right_offset);
        util::printfXml(3, "<frame_crop_top_offset>%d</frame_crop_top_offset>\n", sps.frame_crop_top_offset);
        util::printfXml(3, "<frame_crop_bottom_offset>%d</frame_crop_bottom_offset>\n", sps.frame_crop_bottom_offset);
    }

    util::printfXml(2, "<vui_parameters_present_flag>%d</vui_parameters_present_flag>\n", sps.vui_parameters_present_flag);

    //if (vui_parameters_present_flag)
    //    processVuiParameters(bs);

    util::printfXml(1, "</SPS>\n");
}
void printNalData(const NALData& nalData)
{
    printSpsData(nalData.sequence_parameter_set);
}

size_t mptsParser::processVideoFrames(uint8_t* p,
                                      size_t PESPacketDataLength,
                                      mpts_frame* pFrame)
{
    uint8_t* pStart = p;
    size_t bytesProcessed = 0;
    bool bDone = false;
    PES_packet pes_packet;
    unsigned int framesReceived = 0;
    unsigned int framesWanted = 1;
    static unsigned int frameNumber = 0;

    while (bytesProcessed < (PESPacketDataLength - 4) && !bDone)
    {
    RETRY:
        uint32_t start_code = util::read4Bytes(p);
        uint32_t start_code_prefix = (start_code & 0xFFFFFF00) >> 8;

        if (0x000001 != start_code_prefix)
        {
            fprintf(stderr, "WARNING: Bad data found %llu bytes into this frame.  Searching for next start code...\n", bytesProcessed);
            size_t count = util::nextStartCode(p, PESPacketDataLength);

            if (-1 == count)
            {
                bDone = true;
                continue;
            }

            goto RETRY;
        }

        start_code &= 0x000000FF;

        if (start_code >= system_start_codes_begin &&
            start_code <= system_start_codes_end)
        {
            if (framesReceived == framesWanted)
            {
                bDone = true;
            }
            else
            {
                bytesProcessed += processPESPacketHeader(p, PESPacketDataLength, pes_packet);

                /* Not sure this search for the start code is needed, removing for now
                // Sometimes we come out of process_PES_packet_header and we are not at 0x00000001, I don't know why...
                // So, for now, if we are not at 0x00000001, lets find it.

                uint32_t fourBytes = util::read4Bytes(p);

                if(0x00000001 != fourBytes)
                    bytesProcessed += nextNaluStartCode(p);
                */
            }

            //            continue;
        }

        switch(pFrame->streamType)
        {
            case eH264_Video:
            {
                NALData returnData = { 0 };
                std::any a = &returnData;
                bytesProcessed += m_parser->processVideoFrame(p, PESPacketDataLength - bytesProcessed, a);
                framesReceived = framesWanted;

                // NALData here
                printNalData(returnData);

                printfXml(1, "<frame number=\"%d\" name=\"%s\" packets=\"%d\" pid=\"0x%x\">\n",
                    pFrame->frameNumber++, pFrame->pidList[0].pidName.c_str(), pFrame->totalPackets, pFrame->pid);

                printfXml(2, "<DTS>%llu (%f)</DTS>\n", pes_packet.DTS, convertTimeStamp(pes_packet.DTS));
                printfXml(2, "<PTS>%llu (%f)</PTS>\n", pes_packet.PTS, convertTimeStamp(pes_packet.PTS));

                if (eAVCNaluType_CodedSliceIdrPicture == returnData.picture_type)
                {
                    util::printfXml(2, "<closed_gop>%d</closed_gop>\n", 1);
                }

                assert(returnData.access_unit_delimiter.primary_pic_type < 3);
                util::printfXml(2, "<type>%c</type>\n", "IPB"[returnData.access_unit_delimiter.primary_pic_type]);

                printfXml(2, "<slices>\n");

                for (mptsPidListType::size_type i = 0; i != pFrame->pidList.size(); i++)
                    printfXml(3, "<slice byte=\"%llu\" packets=\"%d\"/>\n", pFrame->pidList[i].pidByteLocation, pFrame->pidList[i].numPackets);

                printfXml(2, "</slices>\n");

                printfXml(1, "</frame>\n");
            }
            break;

            case eMPEG2_Video:
                printfXml(1, "<frame number=\"%d\" name=\"%s\" packets=\"%d\" pid=\"0x%x\">\n",
                    pFrame->frameNumber++, pFrame->pidList[0].pidName.c_str(), pFrame->totalPackets, pFrame->pid);

                printfXml(2, "<DTS>%llu (%f)</DTS>\n", pes_packet.DTS, convertTimeStamp(pes_packet.DTS));
                printfXml(2, "<PTS>%llu (%f)</PTS>\n", pes_packet.PTS, convertTimeStamp(pes_packet.PTS));

                bytesProcessed += m_parser->processVideoFrames(p, PESPacketDataLength - bytesProcessed, frameNumber, framesWanted, framesReceived);

                printfXml(2, "<slices>\n");

                for (mptsPidListType::size_type i = 0; i != pFrame->pidList.size(); i++)
                    printfXml(3, "<slice byte=\"%llu\" packets=\"%d\"/>\n", pFrame->pidList[i].pidByteLocation, pFrame->pidList[i].numPackets);

                printfXml(2, "</slices>\n");

                printfXml(1, "</frame>\n");

                break;
        }

        if (framesWanted == framesReceived)
            bDone = true;
    }

    return p - pStart;
}

size_t mptsParser::processVideoFrames(uint8_t *p,
                                      size_t PESPacketDataLength,
                                      eMptsStreamType streamType,
                                      unsigned int& frameNumber, // Will be incremented by 1 per parsed frame
                                      unsigned int framesWanted,
                                      unsigned int &framesReceived)
{
    uint8_t *pStart = p;
    size_t bytesProcessed = 0;
    bool bDone = false;
    framesReceived = 0;

    while(bytesProcessed < (PESPacketDataLength - 4) && !bDone)
    {
RETRY:
        uint32_t start_code = util::read4Bytes(p);
        uint32_t start_code_prefix = (start_code & 0xFFFFFF00) >> 8;

        if(0x000001 != start_code_prefix)
        {
            fprintf(stderr, "WARNING: Bad data found %llu bytes into this frame.  Searching for next start code...\n", bytesProcessed);
            size_t count = util::nextStartCode(p, PESPacketDataLength);

            if(-1 == count)
            {
                bDone = true;
                continue;
            }

            goto RETRY;
        }

        start_code &= 0x000000FF;

        if(start_code >= system_start_codes_begin &&
           start_code <= system_start_codes_end)
        {
            if(framesReceived == framesWanted)
            {
                bDone = true;
            }
            else
            {
                PES_packet pes_packet;
                
                bytesProcessed += processPESPacketHeader(p, PESPacketDataLength, pes_packet);

                /* Not sure this search for the start code is needed, removing for now
                // Sometimes we come out of process_PES_packet_header and we are not at 0x00000001, I don't know why...
                // So, for now, if we are not at 0x00000001, lets find it.

                uint32_t fourBytes = util::read4Bytes(p);

                if(0x00000001 != fourBytes)
                    bytesProcessed += nextNaluStartCode(p);
                */
            }

//            continue;
        }

        //switch(streamType)
        //{
        //    case eMPEG2_Video:
                bytesProcessed += m_parser->processVideoFrames(p, PESPacketDataLength - bytesProcessed, frameNumber, framesWanted, framesReceived);
        //    break;
        //}

        if(framesWanted == framesReceived)
            bDone = true;
    }

    return p - pStart;
}

// Is this mpts from an OTA broadcast (188 byte packets) or a BluRay (192 byte packets)?
// See: https://github.com/lerks/BluRay/wiki/M2TS
int mptsParser::determine_packet_size(uint8_t buffer[5])
{
    if(SYNC_BYTE == buffer[0])
        m_packetSize = 188;
    else if(SYNC_BYTE == buffer[4])
        m_packetSize = 192;
    else
        return -1;

    return m_packetSize;
}

void mptsParser::flush()
{
    printFrameInfo(&m_videoFrame);
}
