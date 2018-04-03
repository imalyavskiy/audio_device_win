#ifndef __CONVERTER_INTERFACE_H__
#define __CONVERTER_INTERFACE_H__
#pragma once

struct PCMFormat
{
    enum sample_format
    {
        uns = 0, // unspecified
        flt = 1, // ieee 754 float
        ui8 = 2, // unsigned int 8
        i16 = 3, // signed int 16
        i24 = 4, // signed int 24
        i32 = 5, // signed int 32
    };

    bool operator ==(const PCMFormat& other) const
    {
        return sampleFormat == other.sampleFormat &&
               samplesPerSecond == other.samplesPerSecond &&
               channels == other.channels &&
               bitsPerSample == other.bitsPerSample &&
               bytesPerFrame == other.bytesPerFrame;
    }

    sample_format sampleFormat;
    uint32_t      samplesPerSecond;
    uint16_t      channels;
    uint32_t      bitsPerSample;
    uint32_t      bytesPerFrame;
};

struct PCMDataBuffer
{
    typedef std::shared_ptr<PCMDataBuffer> sptr;
    typedef std::weak_ptr<PCMDataBuffer> wptr;
    
    PCMDataBuffer(int8_t* p, std::streamsize total)
        : actual_size(0)
        , end_of_stream(false)
        , total_size(total)
    {
        (*this).p.reset(p);
    }

    inline void reset() { actual_size = 0; end_of_stream = 0; };

    // buffer
    std::unique_ptr<int8_t[]> p; // pointer to modifiable data

    // total
    const std::streamsize total_size;

    // actual
    std::streamsize actual_size;

    // is it the last buffer in the sequence
    bool     end_of_stream;
};

struct ConverterInterface
{
    typedef std::shared_ptr<ConverterInterface> ptr;

    virtual ~ConverterInterface() {};

    virtual bool convert(PCMDataBuffer& in, PCMDataBuffer& out, bool no_more_data) = 0;
};

bool CreateConverter(const PCMFormat& format_in, const PCMFormat& format_out, std::shared_ptr<ConverterInterface>& p);

#endif // __CONVERTER_INTERFACE_H__
