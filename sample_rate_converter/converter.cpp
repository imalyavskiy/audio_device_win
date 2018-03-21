#include "stdafx.h"
#include "converter_interface.h"
#include "converter.h"

bool CreateConverter(const PCMFormat& format_in, const PCMFormat& format_out, std::shared_ptr<ConverterInterface>& p)
{
    p.reset();

    std::shared_ptr<Converter> _p = std::make_shared<Converter>(format_in, format_out);
    if(!_p->initialize())
        return false;

    p = std::static_pointer_cast<ConverterInterface>(_p);

    return (bool)p;
}

Converter::Converter(const PCMFormat& format_in, const PCMFormat& format_out)
    : m_converter_inst(nullptr)
    , m_format_in(format_in)
    , m_format_out(format_out)
{

}

Converter::~Converter()
{
    ;
}

bool Converter::initialize()
{
    int error = 0;

    m_converter_inst = src_new(SRC_SINC_FASTEST, m_format_in.channels, &error);

    return false;
}

bool Converter::convert(const PCMDataBuffer& buffer_in, const PCMDataBuffer& buffer_out)
{
    return false;
}
