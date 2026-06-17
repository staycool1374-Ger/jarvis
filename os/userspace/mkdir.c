#include <unistd.h>

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    return mkdir(argv[1], 0) == 0 ? 0 : 1;
}
