/**
 * @brief Child Process Manager header file
 * @version 1.0.0
 * @author Wolfram Rösler
 * @date 2016-10-20
 * @copyright MIT license
 */

#pragma once

#include <future>
#include <functional>
#include <string>
#include <vector>
#include <sys/types.h>

/**
 * Child process manager class.
 *
 * This class is used to run an executable in a child process, and have
 * it terminated and waited for in the dtor. It encapsulates the Unix
 * fork/exec/kill/wait procedures and provides a more C++-like API for
 * specifying command line parameters.
 */
class ChildProcess {
public:
    // I/O redirection options
    enum Flags {
        IN      = 1<<0,                 ///< Write into standard input
        OUT     = 1<<1,                 ///< Read from standard output
        ERR     = 1<<2                  ///< Read from standard error output
    };

    // Ctor/dtor
    explicit ChildProcess(
        const std::string& exe,
        std::vector<std::string> const& args={},
        int flags=0,
        std::function<void()> init=[](){}
    );
    ChildProcess(ChildProcess &&) noexcept;
    ~ChildProcess();

    // No copying
    ChildProcess(const ChildProcess&) = delete;
    void operator=(const ChildProcess&) = delete;
    void operator=(const ChildProcess&&) = delete;

    // Wait for process to terminate
    int join();

    // Piping
    std::future<void> make_stdin(std::function<void(std::ostream&)>);
    std::future<void> get_stdout(std::function<void(std::istream&)>);
    std::future<void> get_stderr(std::function<void(std::istream&)>);

private:
    pid_t pid_ = 0;                     // PID of the process we started (0=none)
    int pipein_[2]  = { -1, -1 };       // stdin pipe file descriptors
    int pipeout_[2] = { -1, -1 };       // stdout pipe file descriptors
    int pipeerr_[2] = { -1, -1 };       // stderr pipe file descriptors

    int pipefd(Flags which) const;
};
