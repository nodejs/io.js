/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#ifndef QATZPP_QAT_INSTANCE_HPP
#define QATZPP_QAT_INSTANCE_HPP

#include <qat/cpa_dc.h>

#include <memory>
#include <mutex>
#include <vector>

namespace qat
{

class Instance
{
public:
    Instance(CpaInstanceHandle);
    ~Instance();

    CpaInstanceHandle GetHandle() { return instance_; }
    CpaDcInstanceCapabilities GetCapabilities();
    CpaInstanceInfo2 GetInfo();

    int Start(void);
    static std::vector<std::shared_ptr<Instance>> Create();
private:

    CpaInstanceHandle instance_;
    uint16_t num_intermediate_buffer_lists_;
    CpaBufferList **intermediate_buffer_array_;
    bool started_;

    std::mutex mutex_;
};

}

#endif