# Child Process Management Library

Features:

* Run a child process in the background
* Specify exact parameters, not a shell command line
* Write into the process' standard input
* Read from the process' standard output and standard error
* Wait until the process has terminated
* Get the process' exit status
* In short, encapsulates the Unix fork/exec/kill/wait system calls

Also:

* Send a termination signal to the process (in the dtor)
* Run an initialization function in the child process
* Thread-safe
* Exception-safe
* Can be used instead of system(3) and popen(3) in  [CERT](https://en.wikipedia.org/wiki/CERT_C_Coding_Standard)-compliant applications

Prerequisites:

* C++17
* [Boost](https://www.boost.org/)
* Unix-like system

## How to build and run the test program

This repository contains the child process library ([childprocess.hpp](childprocess.hpp), [childprocess.cpp](childprocess.cpp)) together with a Boost.Test unit test program. To build and run the unit tests:

    $ mkdir build
    $ cd build
    $ cmake ..
    $ make
    $ make test

## How to use it in your own projects

Copy [childprocess.hpp](childprocess.hpp) and [childprocess.cpp](childprocess.cpp) to locations of your choise and add them to your build settings. Install and link with boost (see [CMakeLists.txt](CMakeLists.txt) for reference).

## Examples

Reference code only. For complete examples refer to the unit test program ([test.cpp](test.cpp)).

### Start a process and wait for termination

Run `make -j test`, wait for completion, and check exit status.

```cpp
#include <childprocess.hpp>

auto chld = sdb::ChildProcess("/usr/bin/make", { "-j", "test" });
const auto status = child.join();
if (status!=0) {
    // Failed
}
```

### Write to stdin, read from stdout

Run sed to convert `Hello world` into `Good night world`.

```cpp
#include <childprocess.hpp>

auto chld = sdb::ChildProcess(
    "/bin/sed",
    { "s/Hello/Good night/g" },
    sdb::ChildProcess::IN | sdb::ChildProcess::OUT
);

auto in = chld.make_stdin([&data](std::ostream& os) {
    os << "Hello world\n";
});

std::string input;
auto out = chld.get_stdout([&input](std::istream& is) {
    input.assign(std::istreambuf_iterator<char>(is),std::istreambuf_iterator<char>());
});

in.get();
out.get();

const auto status = chld.join();
if (status!=0) {
    // Failed
}

// Now input is "Good night world"
```

---
*Wolfram Rösler • wolfram@roesler-ac.de • https://gitlab.com/wolframroesler • https://twitter.com/wolframroesler • https://www.linkedin.com/in/wolframroesler/*
