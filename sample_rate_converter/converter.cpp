#include "stdafx.h"
#include "converter_interface.h"
#include "converter.h"

Converter::Converter()
    : m_converter_inst(nullptr)
{

}

Converter::~Converter()
{

}

bool Converter::initialize(const uint32_t samplesPerSecond, const uint16_t channels, const uint32_t bitsPerSample)
{
    int error = 0;

    m_samplesPerSecond = samplesPerSecond;
    m_channels = channels;
    m_bitsPerSample = bitsPerSample;

    m_converter_inst = src_new(SRC_SINC_FASTEST, m_channels, &error);

    return false;
}

bool Converter::convert()
{
    return false;
}
