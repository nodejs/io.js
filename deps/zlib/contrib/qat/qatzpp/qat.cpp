/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * Authors:
 *  Gustavo A Espinoza   <gustavo.adolfo.espinoza.quintero@intel.com>
 *                       <gustavoaespinozaq@hotmail.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */
#include "qat.hpp"

#include <qat/cpa.h>
#include <qat/icp_sal_user.h>
#include <qat/qae_mem.h>

#include <iostream>
#include <string>
#include <memory>
#include <mutex>

namespace qat
{

static bool g_qat_not_available = false;
static bool g_qat_initialized = false;
static std::mutex g_qat_initialization_mutex;

class QATContext
{
public:
    explicit QATContext() {}

    QATContext(const QATContext &) = delete;
    QATContext &operator=(const QATContext &) = delete;

    QATContext(QATContext &&) = delete;
    QATContext &operator=(QATContext &&) = delete;

    ~QATContext()
    {
        std::lock_guard<std::mutex> lock(g_qat_initialization_mutex);

        if (g_qat_not_available) return;

        if (g_qat_initialized) {
            icp_sal_userStop();
            g_qat_initialized = false;
        }
    }
};

static std::unique_ptr<QATContext> qat_context;

int Initialize()
{
    std::lock_guard<std::mutex> lock(g_qat_initialization_mutex);
    uint32_t cpa_state;
    if (g_qat_not_available) {
        return CPA_STATUS_FAIL;
    }
    if (g_qat_initialized) {
        return CPA_STATUS_SUCCESS;
    }

    cpa_state = icp_sal_userStartMultiProcess("SSL", CPA_FALSE);

    g_qat_not_available = (cpa_state != CPA_STATUS_SUCCESS);
    g_qat_initialized = (cpa_state == CPA_STATUS_SUCCESS);

    qat_context = std::make_unique<QATContext>();
    return cpa_state;
}

}
