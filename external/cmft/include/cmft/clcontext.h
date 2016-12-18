/*
 * Copyright 2014 Dario Manesku. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef CMFT_CLCONTEXT_H_HEADER_GUARD
#define CMFT_CLCONTEXT_H_HEADER_GUARD

#include <stdint.h>

#define CMFT_CL_VENDOR_INTEL             (0x1)
#define CMFT_CL_VENDOR_AMD               (0x2)
#define CMFT_CL_VENDOR_NVIDIA            (0x4)
#define CMFT_CL_VENDOR_OTHER             (0x8)
#define CMFT_CL_VENDOR_ANY_GPU           (CMFT_CL_VENDOR_AMD|CMFT_CL_VENDOR_NVIDIA)
#define CMFT_CL_VENDOR_ANY_CPU           (CMFT_CL_VENDOR_AMD|CMFT_CL_VENDOR_INTEL)

#define CMFT_CL_DEVICE_TYPE_DEFAULT      (1 << 0)     /* CL_DEVICE_TYPE_DEFAULT */
#define CMFT_CL_DEVICE_TYPE_CPU          (1 << 1)     /* CL_DEVICE_TYPE_CPU */
#define CMFT_CL_DEVICE_TYPE_GPU          (1 << 2)     /* CL_DEVICE_TYPE_GPU */
#define CMFT_CL_DEVICE_TYPE_ACCELERATOR  (1 << 3)     /* CL_DEVICE_TYPE_ACCELERATOR */
#define CMFT_CL_DEVICE_TYPE_ALL          (0xFFFFFFFF) /* CL_DEVICE_TYPE_ALL */

namespace cmft
{
    // OpenCl.
    //----

    int32_t clLoad();
    void    clPrintDevices();
    int32_t clUnload();


    // ClContext.
    //-----

    struct ClContext;

    ClContext* clInit(uint32_t _vendor              = CMFT_CL_VENDOR_ANY_GPU
                    , uint32_t _preferredDeviceType = CMFT_CL_DEVICE_TYPE_GPU
                    , uint32_t _preferredDeviceIdx  = 0
                    , const char* _vendorStrPart    = NULL
                    );
    void       clDestroy(ClContext* _context);

} // namespace cmft

#endif //CMFT_CLCONTEXT_H_HEADER_GUARD

/* vim: set sw=4 ts=4 expandtab: */
