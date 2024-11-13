/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#include <qat/cpa.h>
#include <qat/icp_sal_poll.h>

#include "qat_task.hpp"

namespace qat
{

QATTask::QATTask(std::shared_ptr<Instance> &qat_instance,
    std::unique_ptr<IOBuffers> &&buffers,
    std::unique_ptr<CpaDcRqResults> &&dc_results):
    qat_instance_(qat_instance),
    io_buffers_(std::move(buffers)),
    dc_results_(std::move(dc_results)),
    completed_(false)
{
}

void QATTask::WaitCompletion()
{
    if (completed_) {
        return;
    }

    while (!completed_) {
        icp_sal_DcPollInstance(qat_instance_->GetHandle(), 0);
    }
}

IOBuffers *QATTask::GetBuffers()
{
    return io_buffers_.get();
}

CpaDcRqResults *QATTask::GetResults()
{
    return dc_results_.get();
}

void dc_callback(void *callback_tag, CpaStatus status)
{
    if (!callback_tag) {
        return;
    }
    // Ugly and dangerous
    QATTask* task = static_cast<QATTask*>(callback_tag);
    task->completed_ = true;
}

}