// sample_rate_converter_test.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

int main(int argc, char const ** argv)
{
    if (argc < 3)
    {
        std::cout << "Error: Not enough parameters." << std::endl;
        std::cout << "\tfirst parameter  - input file" << std::endl;
        std::cout << "\tsecond parameter - output file" << std::endl;
        return 1;
    }
    else
    {
        std::cout << "Using: " << argv[1] << " as input." << std::endl;
        std::cout << "Using: " << argv[2] << " as output." << std::endl;
    }

    std::ifstream file_in;
    std::ofstream file_out;

    bool result = false;

    std::shared_ptr<ConverterInterface> converter;

    // $(SolutionDir)44100_16bit_2ch_LittleEndian_short.raw
    const PCMFormat format_in   { PCMFormat::i16, 44100, 2, 16, 4 }; // freq, chan, bits per sample, bytes per frame == ch * bps / 8, where bps % 8 == 0
    // output params
    const PCMFormat format_out  { PCMFormat::i16, 48000, 2, 16, 4 }; // freq, chan, bits per sample, bytes per frame == ch * bps / 8, where bps % 8 == 0

    result = CreateConverter(format_in, format_out, converter);
    if (!result)
    {
        std::cout << "Error: Failed to initialize converter." << std::endl;
        return 1;
    }

    file_in.open(argv[1], std::ios_base::in | std::ios_base::binary);
    file_out.open(argv[2], std::ios_base::out | std::ios_base::binary);

    std::ios_base::iostate rdstate  = std::ios_base::goodbit;
    
    file_in.seekg(0, std::ios_base::end);

    const std::streampos file_size  = file_in.tellg();
          std::streampos curr_pos(0);

    file_in.seekg(0, std::ios_base::beg);

    const uint32_t read_frames      = format_in.samplesPerSecond / 100;
    const uint32_t read_chunk_size  = read_frames *  format_in.channels *  format_in.bitsPerSample / 8;

    const uint32_t write_frames     = format_out.samplesPerSecond / 100;
    const uint32_t write_chunk_size = write_frames * format_out.channels * format_out.bitsPerSample / 8;

    PCMDataBuffer buffer_in(new int8_t[ read_chunk_size * 2 ],  read_chunk_size * 2);
    PCMDataBuffer buffer_out(new int8_t[ write_chunk_size * 2 ], write_chunk_size * 2);

    while (rdstate == std::ios_base::goodbit)
    {
        if (buffer_in.actual_size + read_chunk_size > buffer_in.total_size)
        {
            std::cout << "Error: Input data amount exceeded input buffer size." << std::endl;
            return 1;
        }

        file_in.read(((char*)buffer_in.p.get() + buffer_in.actual_size), read_chunk_size);
        if (std::ios_base::goodbit != (rdstate = file_in.rdstate()))
            continue;
        
        const std::streamsize read_bytes = file_in.gcount();
        buffer_in.actual_size += (uint32_t)read_bytes;

        if (!converter->convert(buffer_in, buffer_out, read_bytes < read_chunk_size))
        {
            std::cout << "Error: while processing data." << std::endl;
            return 1;
        }

        file_out.write((char*)buffer_out.p.get(), buffer_out.actual_size);
        if (std::ios_base::goodbit != file_out.rdstate())
        {
            std::cout << "Error: while writing processed data." << std::endl;
            return 1;
        }
    }

    std::cout << "Success!" << std::endl;

    return 0;
}

