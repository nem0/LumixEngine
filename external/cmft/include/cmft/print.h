/*
 * Copyright 2014 Dario Manesku. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef CMFT_PRINT_H_HEADER_GUARD
#define CMFT_PRINT_H_HEADER_GUARD

namespace cmft
{
    typedef int (*PrintFunc)(const char* _format, ...);

    void setWarningPrintf(PrintFunc _printf);
    void setInfoPrintf(PrintFunc _printf);

} // namespace cmft

#endif //CMFT_PRINT_H_HEADER_GUARD

/* vim: set sw=4 ts=4 expandtab: */
