//
// Created by BaseDCaTx on 1/20/2025.
//

#include <iostream>
#include <vector>
#include <cstring>

std::vector<int> computeLPSArray(const std::string &pattern) {
    std::vector<int> lps(pattern.length(), 0);
    int len = 0;
    lps[0] = 0;
    int i = 1;

    while (i < pattern.length()) {
        if (pattern[i] == pattern[len]) {
            len++;
            lps[i] = len;
            i++;
        } else {
            if (len != 0) {
                len = lps[len - 1];
            } else {
                lps[i] = 0;
                i++;
            }
        }
    }

    return lps;
}

std::vector<uint32_t> kmpSearch(const std::string &text, const std::string &pattern) {
    if (pattern.length() > text.length()) {
        std::cerr << "Pattern length is greater than text length" << std::endl;
        return {};
    }

    std::vector<uint32_t> result;

    std::vector<int> lps = computeLPSArray(pattern);

    int i = 0, j = 0;

    while (i < text.length()) {
        if (text[i] == pattern[j]) {
            i++;
            j++;
        }

        if (j == pattern.length()) {
            std::cout << "Pattern found at index " << i - j << std::endl;
            result.push_back(i - j);
            j = lps[j - 1];
        } else if (i < text.length() && text[i] != pattern[j]) {
            if (j != 0) {
                j = lps[j - 1];
            } else {
                i++;
            }
        }
    }

    return result;
}



