/**
 * @brief Child Process Manager implementation
 * @version 1.0.0
 * @author Wolfram Rösler
 * @date 2016-10-20
 * @copyright MIT license
 */

#include <filesystem>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <sys/wait.h>
#include <ext/stdio_filebuf.h>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>

#include "childprocess.hpp"

using namespace std::chrono_literals;

/**
 * Run a program in a child process.
 *
 * The program will continue running in the background after the function returns.
 * It will be terminated in the destructor of the ChildProcess object.
 *
 * To pipe to/from the process, set `flags` to identify the file descriptors you want
 * to use, and then use `make_stdin`, `get_stdout`, and/or `get_stderr` to communicate with
 * the process.
 *
 * To perform additional initialization (e. g. to change the work directory or set
 * environment variables), `init` is invoked in the child process before executing the
 * new program. If `init` throws an exception, a message is written to stderr (which
 * may be captured with get_stderr), and the child process terminates with EXIT_FAILURE.
 * If `init` is a lambda function, be careful with captured variables because `init` may
 * be called after the ChildProcess ctor has already returned.
 *
 * Note that no shell is involved, so putting something like ">filename" into args
 * will not work. If you need to execute a shell command, set `exe` to "/bin/sh" and
 * args to something like { "-c", "your | command >your.output" }.
 *
 * @param exe Full path name of the program to execute.
 * @param args Command line arguments (excluding the program name). May be empty or omitted.
 * @param flags Combination of IN, OUT, and ERR; determine which fds are available for piping.
 * @param init Initialization function, invoked in the child process. May throw.
 *
 * @throws std::exception if an error occurs.
 *
 * If an error occurs executing the program, the child process writes a message to cerr
 * and terminates. In this case, no exception is thrown (the ctor has already returned
 * in the calling process).
 */
ChildProcess::ChildProcess(
    const std::string& exe,
    std::vector<std::string> const& args,
    int flags,
    std::function<void()> init

) {
    // Make sure the executable exists
    if (!std::filesystem::exists(exe)) {
        throw std::runtime_error("Executable not found: " + exe);
    }

    // Local function to create a pipe
    auto make_pipe = [](int fds[2]){
        if (pipe(fds)) {
            const auto err = errno;
            throw std::runtime_error("Error " + std::to_string(err) + " creating the pipe");
        }
    };

    // The following must not run more than once at the same time.
    // pipe and fork or both together or whatever seem not to be
    // thread-safe. If you don't believe it, comment out the lock_guard
    // line, run the `parallel` test case and see what happens. That
    // might also be the reason why popen isn't completely thread-safe.
    {
        static std::mutex mutex;
        std::lock_guard<std::mutex> _(mutex);

        // Create the pipes needed by the caller
        if (flags & IN)  { make_pipe(pipein_);  }
        if (flags & OUT) { make_pipe(pipeout_); }
        if (flags & ERR) { make_pipe(pipeerr_); }

        // Make a new process
        pid_ = fork();
    }

    switch(pid_) {
        default: {
            // Parent process
            if (flags & IN)  { close(pipein_[0]);  }
            if (flags & OUT) { close(pipeout_[1]); }
            if (flags & ERR) { close(pipeerr_[1]); }
            break;
        }

        case 0: {
            // Child process

            // Handle the pipes
            if (flags & IN)  { close(pipein_[1]);  dup2(pipein_[0],  STDIN_FILENO);  }
            if (flags & OUT) { close(pipeout_[0]); dup2(pipeout_[1], STDOUT_FILENO); }
            if (flags & ERR) { close(pipeerr_[0]); dup2(pipeerr_[1], STDERR_FILENO); }

            // Run the initialization function
            try {
                init();
            } catch (const std::exception& e) {
                std::cerr << "ChildProcess: Exception in initialization function: " << e.what() << std::endl;
                exit(EXIT_FAILURE);
            } catch (...) {
                std::cerr << "ChildProcess: Exception in initialization function" << std::endl;
                exit(EXIT_FAILURE);
            }

            // Make argument vector
            const auto argv = std::unique_ptr<char*[]>(new char*[args.size() + 2]);
            auto i=0;
            argv[i++] = strdup(exe.c_str());
            for(const auto& a: args) {
                argv[i++] = strdup(a.c_str());
            }
            argv[i] = nullptr;

            // Run the executable
            execv(exe.c_str(),argv.get());

            // Failed
            const int err = errno;
            std::cerr << "ChildProcess: Error " << err << " executing " + exe << std::endl;
            exit(EXIT_FAILURE);
        }

        case -1: {
            // Failure
            pid_ = 0;
            const auto err = errno;
            throw std::runtime_error("Error " + std::to_string(err) + " forking a new process");
        }
    }
}

/**
 * Move constructor for ChildProcess.
 */
ChildProcess::ChildProcess(ChildProcess &&rhs) noexcept {
    std::swap(pid_,      rhs.pid_);
    std::swap(pipein_,   rhs.pipein_);
    std::swap(pipeout_,  rhs.pipeout_);
    std::swap(pipeerr_,  rhs.pipeerr_);
}

/**
 * Terminate the process that was started in the constructor by sending SIGTERM.
 * Then, wait for the process to finish. If the process doesn't exit within 3
 * seconds, terminate it with SIGKILL.
 */
ChildProcess::~ChildProcess() {
    if (pid_) {
        // Tell the child to terminate
        kill(pid_,SIGTERM);

        // Give it some time to do so
        for(int count=300;count>=0;--count) {
            if (waitpid(pid_,nullptr,WNOHANG)!=0) return;
            std::this_thread::sleep_for(10ms);
        }

        // Didn't terminate in time, kill it
        kill(pid_,SIGKILL);

        // Zombie trap
        waitpid(pid_,nullptr,0);
    }
}

/**
 * Wait for the child process to terminate.
 *
 * @returns the process' exit status (-1 if not available).
 */
int ChildProcess::join() {
    int ret = -1;

    if (pid_) {
        if (waitpid(pid_,&ret,0)==pid_) {
            pid_ = 0;
        }
    }

    return ret;
}

/**
 * Get a file descriptor of a pipe connected to the process.
 *
 * @param which specifies which file descriptor to return.
 *
 * @returns the requested file descriptor.
 *
 * @throws std::exception if no pipe to that fd was specified in the ctor.
 */
int ChildProcess::pipefd(ChildProcess::Flags which) const {

    // Get the requested file descriptor array
    const auto fds =
        which==IN  ? pipein_  :
        which==OUT ? pipeout_ :
        which==ERR ? pipeerr_ : nullptr;
    if (!fds) {
        throw std::runtime_error("Illegal parameter " + std::to_string(which));
    }

    // Get the requested file descriptor from it
    const auto ret = which==IN ? fds[1] : fds[0];
    if (ret < 0) {
        throw std::runtime_error("Pipe for mode " + std::to_string(which) + " not specified in ctor");
    }

    // Return the file descriptor
    return ret;
}

/**
 * Write into the process' standard input. Creates a thread that calls fct
 * which does the actual work. When fct throws, the exception is forwarded
 * to the caller in the call to get() on the future returned by make_stdin.
 *
 * Example:
 *
 *      ChildProcess chld(..., ChildProcess::IN);
 *
 *      auto in = chld.make_stdin([](std::ostream& os) {
 *          os << "This goes into the process' standard input\n";
 *      }
 *
 *      in.get();       // throws if fct throws
 *      chld.join();
 *
 * @param fct Callable that writes into the process.
 *
 * @returns handle to the writer thread.
 */
std::future<void> ChildProcess::make_stdin(std::function<void(std::ostream&)> fct) {
    return std::async(std::launch::async,[](int fd,std::function<void(std::ostream&)> f) {
        boost::iostreams::file_descriptor_sink snk(fd,boost::iostreams::close_handle);
        boost::iostreams::stream<boost::iostreams::file_descriptor_sink> os(snk);
        f(os);
    },pipefd(IN),fct);
}

/**
 * Read from the process' standard output. Creates a thread that calls fct
 * which does the actual work. When fct throws, the exception is forwarded
 * to the caller in the call to get() on the future returned by get_stdout.
 *
 * Example:
 *
 *      ChildProcess chld(..., ChildProcess::OUT);
 *
 *      auto out = chld.get_stdout([](std::istream& is) {
 *          is >> output_of_the_process;
 *      });
 *
 *      out.get();       // throws if fct throws
 *      chld.join();
 *
 * @param fct Callable that reads from the process.
 *
 * @returns handle to the reader thread.
 */
std::future<void> ChildProcess::get_stdout(std::function<void(std::istream&)> fct) {
    return std::async(std::launch::async,[](int fd,std::function<void(std::istream&)> f) {
        boost::iostreams::file_descriptor_source src(fd,boost::iostreams::close_handle);
        boost::iostreams::stream<boost::iostreams::file_descriptor_source> is(src);
        f(is);
    },pipefd(OUT),fct);
}

/**
 * Read from the process' standard error  output. Creates a thread that calls
 * fct which does the actual work. When fct throws, the exception is forwarded
 * to the caller in the call to get() on the future returned by get_stderr.
 *
 * Example:
 *
 *      ChildProcess chld(..., ChildProcess::ERR);
 *
 *      auto err = chld.get_stderr([](std::istream& is) {
 *          is >> error_output_of_the_process;
 *      });
 *
 *      err.get();       // throws if fct throws
 *      chld.join();
 *
 * @param fct Callable that reads from the process.
 *
 * @returns handle to the reader thread.
 */
std::future<void> ChildProcess::get_stderr(std::function<void(std::istream&)> fct) {
    return std::async(std::launch::async,[](int fd,std::function<void(std::istream&)> f) {
        boost::iostreams::file_descriptor_source src(fd,boost::iostreams::close_handle);
        boost::iostreams::stream<boost::iostreams::file_descriptor_source> is(src);
        f(is);
    },pipefd(ERR),fct);
}
