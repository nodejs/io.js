/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#ifndef QATZPP_QAT_BUFFER_LIST_HPP
#define QATZPP_QAT_BUFFER_LIST_HPP

#include <qat/cpa.h>

#include "io_buffers.h"

namespace qat
{

struct BufferListUser final : public BaseBufferList
{
    BufferListUser(
        uint8_t *data,
        size_t size,
        size_t metadata_size
    );

    ~BufferListUser() override;
};

}

#endif