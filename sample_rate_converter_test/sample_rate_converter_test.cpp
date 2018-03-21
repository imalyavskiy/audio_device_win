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
    const PCMFormat format_in{ 44100,2,16 };
    const PCMFormat format_out{ 48000,2,16 };
    
    result = CreateConverter(format_in, format_out, converter);
    if (!result)
    {
        std::cout << "Error: Failed to initialize converter." << std::endl;
        return 1;
    }

    file_in.open(argv[1], std::ios_base::in | std::ios_base::binary);
    file_out.open(argv[2], std::ios_base::out | std::ios_base::binary);

    std::ios_base::iostate rdstate = std::ios_base::goodbit;
    
    file_in.seekg(0, std::ios_base::end);
    
    const std::streampos file_size = file_in.tellg();
          std::streampos curr_pos(0);
    
    file_in.seekg(0, std::ios_base::beg);

    const uint32_t read_frames      = format_in.samplesPerSecond / 100;
    const uint32_t read_chunk_size  = read_frames * format_in.channels * format_in.bitsPerSample / 8;
    
    const uint32_t write_frames     = (format_out.samplesPerSecond / 100) * 2;
    const uint32_t write_chunk_size = write_frames * format_out.channels * format_out.bitsPerSample / 8;

    PCMDataBuffer buffer_in{ new char[ read_chunk_size ], read_chunk_size, 0 };
    PCMDataBuffer buffer_out{ new char[ write_chunk_size], write_chunk_size, 0 };

    while (rdstate == std::ios_base::goodbit)
    {
        file_in.read((char*)buffer_in.p, buffer_in.tsize);
        if (std::ios_base::goodbit != file_in.rdstate())
            continue;
        buffer_in.asize = file_in.gcount();

        if (!converter->convert(buffer_in, buffer_out))
        {
            std::cout << "Error while processing data." << std::endl;
            break;
        }

        file_out.write((char*)buffer_out.p, buffer_out.asize);
        if (std::ios_base::goodbit != file_out.rdstate())
        {
            std::cout << "Error while writing processed data." << std::endl;
            break;
        }
    }

    return 0;
}

