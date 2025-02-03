/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#include <qat/qae_mem.h>

#include <cstdlib>
#include <iostream>

#include "memory.hpp"
#include "qat.hpp"

namespace qat
{

void *Alloc(size_t size_bytes, uint32_t numa_node)
{
    return qaeMemAllocNUMA(size_bytes, numa_node, 1);
}

void Free(void *ptr)
{
    qaeMemFreeNUMA(&ptr);
}

}