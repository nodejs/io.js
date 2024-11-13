/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#ifndef QATZPP_SESSION_HPP
#define QATZPP_SESSION_HPP

#include <qat/cpa.h>
#include <qat/cpa_dc.h>

#include <memory>

#include "io_buffers.h"
#include "qat_task.hpp"

namespace qat
{

class DeflateSession
{
public:
    DeflateSession(
        std::shared_ptr<Instance> &, CpaDcCompLvl,
        CpaDcChecksum, uint32_t numa_node);
    ~DeflateSession();

    std::unique_ptr<QATTask> Deflate(std::unique_ptr<IOBuffers> &&buffers, bool flush_final);
    std::unique_ptr<QATTask> Inflate(std::unique_ptr<IOBuffers> &&buffers);

    uint32_t GetDeflateBound(uint32_t input_size);

    std::shared_ptr<Instance> getInstance() { return qat_instance_; }

private:
    std::shared_ptr<Instance> qat_instance_;
    CpaDcSessionHandle session_;
};

}

#endif