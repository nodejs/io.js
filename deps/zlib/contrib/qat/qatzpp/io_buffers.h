/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#ifndef QATZPP_IO_BUFFERS_H
#define QATZPP_IO_BUFFERS_H

#include <qat/cpa_dc.h>

#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "memory.hpp"
#include "qat_instance.hpp"

namespace qat
{

struct BaseBufferList
{
    virtual ~BaseBufferList() {}

    CpaBufferList list;
    std::vector<CpaFlatBuffer> flat_buffers;

protected:
    BaseBufferList() {}
};

class IOBuffers
{
public:
    IOBuffers(
        std::unique_ptr<BaseBufferList> &&src_list,
        std::unique_ptr<BaseBufferList> &&dst_list
    );
    virtual ~IOBuffers();

    BaseBufferList *GetSrc() const {
        return src_buffer_list_.get();
    }

    BaseBufferList *GetDst() const {
        return dst_buffer_list_.get();
    }
protected:
    IOBuffers();

    std::unique_ptr<BaseBufferList> src_buffer_list_;
    std::unique_ptr<BaseBufferList> dst_buffer_list_;
};

}

#endif