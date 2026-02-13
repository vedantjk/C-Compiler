#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: cc89 <source.c>\n";
        return 1;
    }

    std::cout << "cc89: " << argv[1] << "\n";
    return 0;
}
