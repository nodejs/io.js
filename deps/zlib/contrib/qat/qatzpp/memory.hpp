/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#ifndef QATZPP_MEMORY_HPP
#define QATZPP_MEMORY_HPP

#include <cstddef>
#include <cstdint>

namespace qat
{

void *Alloc(size_t sizeBytes, uint32_t numa_node);

template <typename T>
T *AllocBlock(int32_t numa_node)
{
    return static_cast<T*>(Alloc(sizeof(T), numa_node));
}

template <typename T>
T *AllocBlockArray(size_t count, int32_t numa_node)
{
    if (count <= 0) {
        return nullptr;
    }

    return static_cast<T*>(Alloc(sizeof(T) * count, numa_node));
}

void Free(void *ptr);

}

#endif