#include <mcutest/mcutest.hpp>
#include <cstdlib>
int main(int argc, char* argv[]) {
    if (std::getenv("MCUTCL_JSON_TEST_OUTPUT") != nullptr) {
        mcutest::Runner<mcutest::JsonOutput> runner;
        return mcutest::run_json_output(runner);
    }
    mcutest::Runner<mcutest::StdoutOutput> runner;
    return mcutest::run_with_gtest_compat(argc, argv, runner);
}
