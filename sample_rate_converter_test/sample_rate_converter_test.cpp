// sample_rate_converter_test.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

int main()
{
    bool result = false;
    PCMFormat format_in{44100, 2, 16};
    PCMFormat format_out{48000, 2, 16};
    std::shared_ptr<ConverterInterface> converter;
    
    result = CreateConverter(format_in, format_out, converter);
    if (!result)
        std::cout << "Failed to initialize converter" << std::endl;

    return 0;
}

