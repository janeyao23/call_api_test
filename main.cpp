#include <iostream>
#include <string>
#include <stdexcept>
#include <curl/curl.h>

int call_api_demo();

int main(int argc, char* argv[]) {
    try {
        call_api_demo();
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return -99;
    }
    catch (...) {
        std::cerr << "Unknown exception" << std::endl;
        return -100;
    }

    return 0;
}
