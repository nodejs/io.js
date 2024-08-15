/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#include <fstream>
#include <iostream>

#include "io_buffers.h"
#include "qat_instance.hpp"

namespace qat
{

IOBuffers::IOBuffers()
{
}

IOBuffers::IOBuffers(std::unique_ptr<BaseBufferList>&& src_list, std::unique_ptr<BaseBufferList>&& dst_list):
    src_buffer_list_(std::move(src_list)), dst_buffer_list_(std::move(dst_list))
{
}

IOBuffers::~IOBuffers()
{
}

}
