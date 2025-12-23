#pragma once

#include <iostream>
#include <string>
#include <time.h>

#define NORMAL 1
#define WARNING 2
#define INFO 3
#define DEBUG 4
#define FATAL 5

#define LOG(LEVEL, MESSAGE) log(#LEVEL, MESSAGE, __FILE__, __LINE__)


void log(std::string level, std::string mesg, std::string file, int line) {
    std::cout << "[" << level << "] " << "[" << time(nullptr) << "] " << "[" << mesg << "] " << "[" << file << " : " << line << "] " << std::endl;
}