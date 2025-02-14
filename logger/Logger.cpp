//
// Created by BaseDCaTx on 1/16/2025.
//

#include <string>
#include <iostream>
#include <cstring>
#include <cerrno>

void LogErrorWithReason(const char *reason, const char *details) {
    std::cerr << reason << " : " << details << std::endl;
}

void LogErrorWithReasonX(const char *reason, const char *details) {
    std::cerr << reason << " : " << details << std::endl;
    exit(EXIT_FAILURE);
}

void LogSystemError(const char *reason) {
    std::cerr << reason << ": " << strerror(errno) << std::endl;
    exit(EXIT_FAILURE);
}