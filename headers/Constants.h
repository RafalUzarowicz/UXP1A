#ifndef LINDA_CONSTANTS_H
#define LINDA_CONSTANTS_H

#include <chrono>

namespace Linda{
    const static int32_t MAX_STRING_LENGTH = 64;
    const static unsigned long MAX_TUPLE_SIZE = 255;
    const static unsigned long ENTRY_SIZE = MAX_TUPLE_SIZE + 2;     //pad so that there is additional char for new line
    const static char EMPTY_FLAG = 'E';
    const static char BUSY_FLAG = 'B';
    const static char INPUT_FLAG = 'I';
    const static char READ_FLAG = 'R';
    const static char LIST_HEADER_SIZE = sizeof(pid_t) + 1;
    const static std::string DEFAULT_TUPLESPACE_NAME = "tuplespace";
    const static std::string DEFAULT_TUPLESPACE_DIR = "./";
    const static std::string DEFAULT_TUPLESPACE_PATH = DEFAULT_TUPLESPACE_DIR + DEFAULT_TUPLESPACE_NAME;
    const static std::chrono::milliseconds DEFAULT_TIMEOUT = std::chrono::milliseconds(10000);
    const static std::chrono::milliseconds DEFAULT_READ_TIMEOUT = DEFAULT_TIMEOUT;
    const static std::chrono::milliseconds DEFAULT_INPUT_TIMEOUT = DEFAULT_TIMEOUT;
}

#endif //LINDA_CONSTANTS_H