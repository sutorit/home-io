#ifdef PC_BUILD
#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1

using String = std::string;
namespace fs = std::filesystem;

// Minimal replacement for SPIFFS
struct MockSPIFFS {
    bool begin(bool formatOnFail = true) {
        if (!fs::exists("spiffs")) {
            fs::create_directory("spiffs");
        }
        return true;
    }
    bool exists(const char* path) {
        return fs::exists(std::string("spiffs") + path);
    }
    std::ifstream openRead(const char* path) {
        return std::ifstream(std::string("spiffs") + path);
    }
    std::ofstream openWrite(const char* path) {
        return std::ofstream(std::string("spiffs") + path);
    }
} SPIFFS;

inline void pinMode(int pin, int mode) {
    std::cout << "[MOCK] pinMode(" << pin << ", " << mode << ")\n";
}
inline void digitalWrite(int pin, int value) {
    std::cout << "[MOCK] digitalWrite(" << pin << ", " << value << ")\n";
}
inline int digitalRead(int pin) {
    return LOW;
}
inline void delay(int ms) {
    std::cout << "[MOCK] delay(" << ms << "ms)\n";
}
#endif
