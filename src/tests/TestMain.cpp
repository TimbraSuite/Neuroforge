#include "TestMain.h"
#include <cstring>
int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (auto& c : nftest::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        int before = nftest::failures();
        std::printf("[ RUN  ] %s\n", c.name);
        c.fn();
        ++run;
        std::printf(nftest::failures() == before ? "[  OK  ] %s\n" : "[FAILED] %s\n", c.name);
    }
    std::printf("\n%d tests, %d failed checks\n", run, nftest::failures());
    return nftest::failures() == 0 ? 0 : 1;
}
