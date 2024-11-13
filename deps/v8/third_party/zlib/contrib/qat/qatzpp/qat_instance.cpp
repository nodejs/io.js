/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#include <qat/qae_mem.h>

#include <iostream>
#include <vector>

#include "memory.hpp"
#include "qat_instance.hpp"
#include "session.hpp"

#define MAX_SAMPLE_BUFFER_SIZE  (4*1024*1024)

namespace qat
{

static std::mutex g_instance_mutex;
static std::vector<std::shared_ptr<Instance>> instances;

static CpaPhysicalAddr virt2Phys(void *virt_addr)
{
    return (CpaPhysicalAddr)qaeVirtToPhysNUMA(virt_addr);
}

Instance::Instance(CpaInstanceHandle instance):
    instance_(instance),
    num_intermediate_buffer_lists_(0),
    intermediate_buffer_array_(nullptr),
    started_(false)
{
    CpaDcInstanceCapabilities caps{};
    cpaDcQueryCapabilities(instance_, &caps);

    if (!caps.statelessDeflateCompression || !caps.statelessDeflateDecompression ||
        !caps.checksumAdler32 || !caps.dynamicHuffman)
    {
        return;
    }

    if (caps.dynamicHuffmanBufferReq) {
        uint32_t buffer_metadata_size;
        cpaDcBufferListGetMetaSize(instance_, 1, &buffer_metadata_size);
        cpaDcGetNumIntermediateBuffers(instance_, &num_intermediate_buffer_lists_);

        if(num_intermediate_buffer_lists_) {
            intermediate_buffer_array_ = AllocBlockArray<CpaBufferList*>(num_intermediate_buffer_lists_, 0);
        }
        for (int i = 0; i < num_intermediate_buffer_lists_; ++i) {
            intermediate_buffer_array_[i] = AllocBlock<CpaBufferList>(0);
            intermediate_buffer_array_[i]->pPrivateMetaData =
                                                    AllocBlockArray<uint8_t>(buffer_metadata_size, 0);
            intermediate_buffer_array_[i]->pBuffers = AllocBlock<CpaFlatBuffer>(0);
            intermediate_buffer_array_[i]->pBuffers->pData =
                                                    AllocBlockArray<uint8_t>(MAX_SAMPLE_BUFFER_SIZE, 0);
            intermediate_buffer_array_[i]->pBuffers->dataLenInBytes = MAX_SAMPLE_BUFFER_SIZE;
        }
    }

    cpaDcSetAddressTranslation(instance_, virt2Phys);
}

Instance::~Instance()
{
}

CpaDcInstanceCapabilities Instance::GetCapabilities()
{
    CpaDcInstanceCapabilities caps{};
    cpaDcQueryCapabilities(instance_, &caps);

    return caps;
}

CpaInstanceInfo2 Instance::GetInfo()
{
    CpaInstanceInfo2 info{};
    cpaDcInstanceGetInfo2(instance_, &info);

    return info;
}

int Instance::Start()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (started_) {
        return 0;
    }

    int ret = cpaDcStartInstance
    (
        instance_,
        num_intermediate_buffer_lists_,
        intermediate_buffer_array_
    );
    if (ret) {
        return -1;
    }
    started_ = true;
    return 0;
}

std::vector<std::shared_ptr<Instance>> Instance::Create()
{
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    uint16_t num_instances = 0;

    if (!instances.empty()) {
        return instances;
    }

    cpaDcGetNumInstances(&num_instances);

    if (!num_instances) {
        std::cerr << "No instances found\n";
        return {};
    }

    std::vector<CpaInstanceHandle> handles(num_instances);
    cpaDcGetInstances(num_instances, handles.data());

    for(auto& handle: handles) {
        instances.emplace_back(std::make_shared<Instance>(handle));
    }

    return instances;
}

}
