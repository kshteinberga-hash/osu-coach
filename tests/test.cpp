#include "t.h"

int main() {
    CHECK(1 + 1 == 2);
    std::printf("%d checks, %d failures\n", t_run, t_fail);
    return t_fail != 0;
}
