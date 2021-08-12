# mpts_parser

(C) 2019, Mike Cancilla


MPEG Transport Stream to XML
----------------------------

Windows command line app that will parse an MPEG Transport Stream and generate an XML document which represents the stream.

Example command line.  This will write xml to output.xml, and it will print the progress.

    C:\> mpts_parser -p file.mpts > output.xml

The XML files generated by this program can be used as input to my mpts_analyzer program.

MPEG files in a transport stream can be found here: https://dveo.com/downloads/VGA2/sample-digital-signage-streams.html
