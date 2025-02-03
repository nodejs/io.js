/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#ifndef QATZPP_WORK_HPP
#define QATZPP_WORK_HPP

#include <qat/cpa.h>

#include <memory>

#include "io_buffers.h"

namespace qat
{

class QATTask
{
public:
    explicit QATTask(std::shared_ptr<Instance> &qat_instance,
                std::unique_ptr<IOBuffers> &&,
                std::unique_ptr<CpaDcRqResults> &&dc_results);

    QATTask(QATTask &&) = delete;
    QATTask& operator=(QATTask &&) = delete;

    QATTask(const QATTask &) = delete;
    QATTask &operator=(const QATTask &) = delete;

    void WaitCompletion();

    IOBuffers *GetBuffers();
    CpaDcRqResults *GetResults();

private:
    bool completed_;

    std::shared_ptr<Instance> qat_instance_;

    std::unique_ptr<CpaDcRqResults> dc_results_;
    std::unique_ptr<IOBuffers> io_buffers_;

    friend void dc_callback(void *, CpaStatus);
};

void dc_callback(void*, CpaStatus);

}

#endif