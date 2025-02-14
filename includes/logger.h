//
// Created by BaseDCaTx on 1/16/2025.
//

#include <string>

#ifndef XHTTP_LOGGER_H
#define XHTTP_LOGGER_H

void LogErrorWithReason(const char *reason, const char *details);

void LogErrorWithReasonX(const char *reason, const char *details);

void LogSystemError(const char *reason);

#endif //XHTTP_LOGGER_H
