/**
 * @brief Child Process Manager header unit tests
 * @version 1.0.0
 * @author Wolfram Rösler
 * @date 2016-10-20
 * @copyright MIT license
 */

#include <filesystem>
#include <fstream>
#include <future>
#include <unordered_set>

#define BOOST_TEST_MODULE childprocess
#include <boost/test/unit_test.hpp>

#include "childprocess.hpp"

BOOST_AUTO_TEST_SUITE(childprocess)

// Fixture class
class Fx {
public:
    const std::string tmpfile = "./childprocess-" + std::to_string(rand());

    ~Fx() {
        std::filesystem::remove(tmpfile);
    }
};

/*
 * Test running a child process
 */
BOOST_FIXTURE_TEST_CASE(exec,Fx) {
    // Our test value
    const auto data = rand();

    // Write the test value to a file using a child process
    auto chld = ChildProcess(
        "/bin/sh", {
            "-c",
            "echo " + std::to_string(data) + " >" + tmpfile + " 2>&1"
        }
    );

    // Wait until the process terminates, and check the exit code
    BOOST_TEST(chld.join()==0);

    // Waiting again returns -1 since we got the exit code already
    BOOST_TEST(chld.join()==-1);

    // Open the output file
    BOOST_TEST(std::filesystem::exists(tmpfile));
    auto ifs = std::ifstream(tmpfile);
    BOOST_TEST(!!ifs);

    // Read what's in it
    int value = -1;
    ifs >> value;

    // Make sure it's what we expect
    BOOST_TEST(value==data);
}

/*
 * Test piping stuff into/out of a process
 */
BOOST_FIXTURE_TEST_CASE(pipe,Fx) {
    // This is our test value:
    const auto data = rand();

    // Make a process we can write to and read from
    auto chld = ChildProcess(
        "/bin/grep",
        { "^" + std::to_string(data) + "$" },
        ChildProcess::IN | ChildProcess::OUT | ChildProcess::ERR
    );

    // Write to the process' standard input
    auto in = chld.make_stdin([&data](std::ostream& os) {
        // First send some random lines
        for(auto _=rand()%10;_>0;--_) {
            os << rand() << "\n";
        }

        // Then send the value we want to extract
        os << data << "\n";

        // Finish with some more random lines
        for(auto _=rand()%10;_>0;--_) {
            os << rand() << "\n";
        }
    });

    // Capture the process' standard output
    int recv = -1;
    auto out = chld.get_stdout([&recv](std::istream& is) {
        is >> recv;
    });

    // Also capture its standard error output
    std::string fromerr;
    auto err = chld.get_stderr([&fromerr](std::istream& is) {
        fromerr.assign(std::istreambuf_iterator<char>(is),std::istreambuf_iterator<char>());
    });

    // Wait until everything is finished
    in.get();
    out.get();
    err.get();
    const auto status = chld.join();

    BOOST_TEST(status==0);
    BOOST_TEST(recv==data);
    BOOST_TEST(fromerr.empty());
}

/*
 * Test parallel piping
 */
BOOST_FIXTURE_TEST_CASE(parallel,Fx) {

    // Use this many parallel processes:
    const int nprocs = 1000;

    // So do it:
    std::vector<std::future<int>> tasks;
    for(auto i=0;i<nprocs;++i) {
        tasks.push_back(std::async(std::launch::async,
            [&](int idx) {

                // Create a child process that reflects its standard input
                ChildProcess chld("/bin/cat",{},ChildProcess::IN | ChildProcess::OUT);

                // Write our thread index (0..count) into it
                auto in = chld.make_stdin([&idx](std::ostream& os) { os << idx << "\n"; });

                // Read out the result - must be identical to the input
                int recv = -1;
                auto out = chld.get_stdout([&recv](std::istream& is) { is >> recv; });

                // Wait for everybody to finish
                in.get();
                out.get();
                chld.join();

                // Return the result, leave validation to the caller
                return recv;
            },
            i)
        );
    }

    // Now count how many different results we got
    std::unordered_set<int> values;
    for(auto& t : tasks) {
        values.insert(t.get());
    }

    // Test if we got the right number of results (one per process)
    BOOST_TEST(values.size()==nprocs);
}

/*
 * Test calling an initialization function in the child process
 */
BOOST_FIXTURE_TEST_CASE(initok,Fx) {

    // Make a random string
    const auto input = std::to_string(rand());

    // Use the init function to put the test string into the environment
    // of our child process, and run a program that outputs it.
    auto chld = ChildProcess(
        "/bin/sh",
        { "-c", "echo $TESTSTRING" },
        ChildProcess::OUT,
        [&input](){ putenv(strdup(("TESTSTRING="+input).c_str())); }
    );

    // Read the child process' output
    std::string output;
    chld.get_stdout([&output](std::istream& is){ std::getline(is,output); }).get();
    chld.join();

    // Compare it
    BOOST_TEST(output==input);
}

/*
 * Test throwing an exception in the initialization function.
 */
BOOST_FIXTURE_TEST_CASE(initfail,Fx) {

    // Make a random string
    const auto input = std::to_string(rand());

    // Run a child process, but with an init function that fails and throws
    // the test string. The ChildProcess ctor will write the exception message
    // to the new process' stderr.
    ChildProcess chld("/bin/sh",
        { "-c", "echo If you can see this, the test has failed >&2" },
        ChildProcess::ERR,
        [&input](){ throw std::runtime_error(input); }
    );

    // Catch the child's stderr: If all goes well, it contains our test string
    std::string output;
    chld.get_stderr([&output](std::istream& is){ std::getline(is,output); }).get();
    chld.join();

    // Compare it
    BOOST_TEST(output.find(input)!=std::string::npos);
}

/*
 * Test throwing exceptions from the in/out/err functions.
 */
BOOST_FIXTURE_TEST_CASE(except,Fx) {

    // Run a process that simply copies stdin to stdout. The process doesn't
    // terminate before we close stdin, which we're testing implicitly.
    // Connect to all three I/O pipes.
    auto chld = ChildProcess("/bin/cat",{},ChildProcess::IN | ChildProcess::OUT | ChildProcess::ERR);

    // The identification numbers we throw around
    const auto exI = rand();
    const auto exO = rand();
    const auto exE = rand();

    // Attach to pipes and throw immediately
    auto in  = chld.make_stdin([&exI](std::ostream&){ throw exI; });
    auto out = chld.get_stdout([&exO](std::istream&){ throw exO; });
    auto err = chld.get_stderr([&exE](std::istream&){ throw exE; });

    // Wait for the threads to terminate and catch their exceptions.
    int gotI=0,gotO=0,gotE=0;
    try { in .get(); } catch(int e) { gotI = e; }
    try { out.get(); } catch(int e) { gotO = e; }
    try { err.get(); } catch(int e) { gotE = e; }

    // Wait for the process to terminate
    chld.join();

    // Compare what we got to what we're expecting
    BOOST_TEST(gotI==exI);
    BOOST_TEST(gotO==exO);
    BOOST_TEST(gotE==exE);
}

BOOST_AUTO_TEST_SUITE_END()
