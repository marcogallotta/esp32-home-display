#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include "log.h"

int main(int argc, char** argv) {
    setLogMuted(true);

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
