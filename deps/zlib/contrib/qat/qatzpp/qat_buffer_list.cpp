/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#include "qat_buffer_list.hpp"

namespace qat
{

BufferListUser::BufferListUser(
    uint8_t *data,
    size_t size,
    size_t metadata_size)
{
    flat_buffers = std::vector<CpaFlatBuffer>(1);
    flat_buffers[0].pData = data;
    flat_buffers[0].dataLenInBytes = size;
    list.pPrivateMetaData = AllocBlockArray<uint8_t>(metadata_size, 0);
    list.numBuffers = 1;
    list.pBuffers = flat_buffers.data();
}

BufferListUser::~BufferListUser()
{
    if (list.pPrivateMetaData) {
        Free(list.pPrivateMetaData);
    }
}

}
