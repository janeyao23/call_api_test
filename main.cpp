#include <iostream>
#include <string>
#include <stdexcept>

int call_api_demo(int argc, char* argv[]);

int main(int argc, char* argv[]) {
    try {
        return call_api_demo(argc, argv);
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return -99;
    }
    catch (...) {
        std::cerr << "Unknown exception" << std::endl;
        return -100;
    }
}
