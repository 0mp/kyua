// Copyright 2010 Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
// * Neither the name of Google Inc. nor the names of its contributors
//   may be used to endorse or promote products derived from this software
//   without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "utils/process/children.ipp"

extern "C" {
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
}

#include <cstdarg>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

#include <atf-c++.hpp>

#include "utils/datetime.hpp"
#include "utils/defs.hpp"
#include "utils/env.hpp"
#include "utils/format/macros.hpp"
#include "utils/fs/operations.hpp"
#include "utils/logging/macros.hpp"
#include "utils/process/exceptions.hpp"
#include "utils/process/system.hpp"
#include "utils/sanity.hpp"
#include "utils/signals/timer.hpp"

namespace datetime = utils::datetime;
namespace fs = utils::fs;
namespace logging = utils::logging;
namespace process = utils::process;
namespace signals = utils::signals;


namespace {


/// Process that the timer will terminate.
static int timer_pid = 0;


/// Callback for a timer to set timer_fired to true.
static void
timer_callback(void)
{
    ::kill(timer_pid, SIGCONT);
}


/// Validates that interrupting the wait call raises the proper error.
///
/// \tparam child The type of the child to validate.
/// \param child The child to validate.
template< class Child >
void
interrupted_check(Child& child)
{
    timer_pid = ::getpid();
    signals::timer timer(datetime::delta(0, 500000), timer_callback);

    std::cout << "Waiting for subprocess; should be aborted\n";
    ATF_REQUIRE_THROW(process::system_error,
                      child->wait(datetime::delta()));

    timer.unprogram();

    std::cout << "Now terminating process for real\n";
    ::kill(child->pid(), SIGKILL);
    const process::status status = child->wait(datetime::delta());
    ATF_REQUIRE(status.signaled());

    ATF_REQUIRE(!fs::exists(fs::path("finished")));
}


/// Body for a process that spawns a subprocess.
///
/// This is supposed to be passed as a hook to one of the fork() functions.  The
/// fork() functions run their children in a new process group, so it is
/// expected that the subprocess we spawn here is part of this process group as
/// well.
static void
child_blocking_subchild(void)
{
    pid_t pid = ::fork();
    if (pid == -1) {
        std::abort();
    } else if (pid == 0) {
        for (;;)
            ::pause();
    } else {
        std::ofstream output("subchild_pid");
        if (!output)
            std::abort();
        output << pid << "\n";
        output.close();
        std::exit(EXIT_SUCCESS);
    }
    UNREACHABLE;
}


/// Ensures that the subprocess started by child_blocking_subchild is dead.
///
/// This function has to be called after running the child_blocking_subchild
/// function through a fork call.  It ensures that the subchild spawned is
/// ready, waits for the process group and ensures that both the child and the
/// subchild have died.
///
/// \param child The child object.
template< class Child >
void
child_blocking_subchild_check(Child child)
{
    const process::status status = child->wait();

    ATF_REQUIRE(status.exited());
    ATF_REQUIRE_EQ(EXIT_SUCCESS, status.exitstatus());

    std::ifstream input("subchild_pid");
    ATF_REQUIRE(input);
    pid_t pid;
    input >> pid;
    input.close();
    std::cout << F("Subprocess was %s; checking if it died\n") % pid;

    int attempts = 30;
retry:
    if (::kill(pid, SIGCONT) != -1 || errno != ESRCH) {
        // Looks like the subchild did not die.
        //
        // Note that this might be inaccurate for two reasons:
        // 1) The system may have spawned a new process with the same pid as
        //    our subchild... but in practice, this does not happen because
        //    most systems do not immediately reuse pid numbers.  If that
        //    happens... well, we get a false test failure.
        // 2) We ran so fast that even if the process was sent a signal to
        //    die, it has not had enough time to process it yet.  This is why
        //    we retry this a few times.
        if (attempts > 0) {
            std::cout << "Subprocess not dead yet; retrying wait\n";
            --attempts;
            ::usleep(100000);
            goto retry;
        }
        ATF_FAIL(F("The subprocess %s of our child was not killed") % pid);
    }
}


/// Body for a process that prints a simple message and exits.
///
/// \tparam ExitStatus The exit status for the subprocess.
/// \tparam Message A single character that will be prepended to the printed
///     messages.  This would ideally be a string, but we cannot templatize a
///     function with an object nor a pointer.
template< int ExitStatus, char Message >
static void
child_simple_function(void)
{
    std::cout << "To stdout: " << Message << "\n";
    std::cerr << "To stderr: " << Message << "\n";
    std::exit(ExitStatus);
}


/// Functor for the body of a process that prints a simple message and exits.
class child_simple_functor {
    /// The exit status that the subprocess will yield.
    int _exitstatus;

    /// The message to print on stdout and stderr.
    std::string _message;

public:
    /// Constructs a new functor.
    ///
    /// \param exitstatus The exit status that the subprocess will yield.
    /// \param message The message to print on stdout and stderr.
    child_simple_functor(const int exitstatus, const std::string& message) :
        _exitstatus(exitstatus),
        _message(message)
    {
    }

    /// Body for the subprocess.
    void
    operator()(void)
    {
        std::cout << "To stdout: " << _message << "\n";
        std::cerr << "To stderr: " << _message << "\n";
        std::exit(_exitstatus);
    }
};


/// Body for a process that prints many messages to stdout and exits.
///
/// The goal of this body is to validate that any buffering performed on the
/// parent process to read the output of the subprocess works correctly.
static void
child_printer_function(void)
{
    for (std::size_t i = 0; i < 100; i++)
        std::cout << "This is a message to stdout, sequence " << i << "\n";
    std::cout.flush();
    std::cerr << "Exiting\n";
    std::exit(EXIT_SUCCESS);
}


/// Functor for the body of a process that runs child_printer_function.
class child_printer_functor {
public:
    /// Body for the subprocess.
    void
    operator()(void)
    {
        child_printer_function();
    }
};


/// Body for a process that sleeps for an amount of time and exits.
///
/// The goal of this body is to validate the timeout functionality of the
/// parent.  This is done by sleeping first and later creating a "cookie" file
/// in the current directory that indicates that the process actually finished
/// its execution.  If the child is killed while it is sleeping, then the cookie
/// is not created and we can check that the timeout worked.
///
/// \tparam Microseconds The time to sleep for before creating the cookie.
template< int Microseconds >
static void
child_wait(void)
{
    std::cout << "Sleeping in subprocess\n";
    if (Microseconds > 1000000)
        ::sleep(Microseconds / 1000000);
    else
        ::usleep(Microseconds);
    std::cout << "Resuming subprocess and exiting\n";
    atf::utils::create_file("finished", "");
    std::exit(EXIT_SUCCESS);
}


/// Body for a process that spawns another process and sleeps.
///
/// The goal of this body is similar to that of child_wait.  However, we
/// generate a "subprocess tree" from here by spawning another subprocess.  This
/// is to allow the caller to validate that, when the timeout for the process is
/// reached, all of the children are killed (i.e. all the process group is
/// terminated), not just the directly-spawned child.  These checks are also
/// performed by file system cookies.
///
/// \tparam Microseconds The time to sleep for before creating the cookies.
template< int Microseconds >
static void
child_wait_with_subchild(void)
{
    const int ret = ::fork();
    if (ret == -1) {
        std::abort();
    } else if (ret == 0) {
        ::usleep(Microseconds);
        atf::utils::create_file("subfinished", "");
        std::exit(EXIT_SUCCESS);
    } else {
        ::usleep(Microseconds);
        atf::utils::create_file("finished", "");

        int status;
        (void)::wait(&status);
        std::exit(EXIT_SUCCESS);
    }
}


/// Body for a child process that creates a pidfile.
static void
child_write_pid(void)
{
    std::ofstream output("pidfile");
    output << ::getpid() << "\n";
    output.close();
    std::exit(EXIT_SUCCESS);
}


/// Validates that the value of the pidfile matches the pid file in the child.
///
/// \tparam Child The type of the child to validate.
/// \param child The child to validate.
template< class Child >
void
child_write_pid_check(Child& child)
{
    const int pid = child->pid();

    const process::status status = child->wait();
    ATF_REQUIRE(status.exited());
    ATF_REQUIRE_EQ(EXIT_SUCCESS, status.exitstatus());

    std::ifstream input("pidfile");
    ATF_REQUIRE(input);
    int read_pid;
    input >> read_pid;
    input.close();

    ATF_REQUIRE_EQ(read_pid, pid);
}


/// A child process that returns.
///
/// The fork() wrappers are supposed to capture this condition and terminate the
/// child before the code returns to the fork() call point.
static void
child_return(void)
{
}


/// A child process that raises an exception.
///
/// The fork() wrappers are supposed to capture this condition and terminate the
/// child before the code returns to the fork() call point.
///
/// \tparam Type The type of the exception to raise.
/// \tparam Value The value passed to the constructor of the exception type.  In
///     general, this only makes sense if Type is a primitive type so that, in
///     the end, the code becomes "throw int(123)".
///
/// \throw Type An exception of the provided type.
template< class Type, Type Value >
void
child_raise_exception(void)
{
    throw Type(Value);
}


/// Functor for the body of a process that calls process::exec.
///
/// In order to be able to test the process::exec function, we must execute it
/// under a subprocess so that we can inspect the actions taken in such
/// subprocess from the test case itself.
class do_exec {
    /// The path to the program to execute.
    fs::path _program;

    /// The arguments to pass to the executed program.
    const std::vector< std::string > _args;

public:
    /// Constructs a new functor.
    ///
    /// \param program The path to the program to execute.
    /// \param args The arguments to pass to the executed program.
    do_exec(const fs::path& program, const std::vector< std::string >& args) :
        _program(program),
        _args(args)
    {
    }

    /// Body for the subprocess.
    void
    operator()(void)
    {
        logging::set_inmemory();
        try {
            process::exec(_program, _args);
        } catch (const process::system_error& e) {
            std::cerr << "Caught system_error: " << e.what() << '\n';
            std::abort();
        }
    }
};


/// Calculates the path to the test helpers binary.
///
/// \param tc A pointer to the caller test case, needed to extract the value of
///     the "srcdir" property.
///
/// \return The path to the helpers binary.
static fs::path
get_helpers(const atf::tests::tc* tc)
{
    return fs::path(tc->get_config_var("srcdir")) / "helpers";
}


/// Mock fork(2) that just returns an error.
///
/// \tparam Errno The value to set as the errno of the failed call.
///
/// \return Always -1.
template< int Errno >
static pid_t
fork_fail(void) throw()
{
    errno = Errno;
    return -1;
}


/// Mock open(2) that fails if the 'raise-error' file is opened.
///
/// \tparam Errno The value to set as the errno if the known failure triggers.
/// \param path The path to the file to be opened.
/// \param flags The open flags.
/// \param ... The file mode creation, if flags contains O_CREAT.
///
/// \return The opened file handle or -1 on error.
template< int Errno >
static int
open_fail(const char* path, const int flags, ...) throw()
{
    if (std::strcmp(path, "raise-error") == 0) {
        errno = Errno;
        return -1;
    } else {
        va_list ap;
        va_start(ap, flags);
        const int mode = va_arg(ap, int);
        va_end(ap);
        return ::open(path, flags, mode);
    }
}


/// Mock pipe(2) that just returns an error.
///
/// \tparam Errno The value to set as the errno of the failed call.
/// \param [out] unused_fildes A pointer to a 2-integer array.
///
/// \return Always -1.
template< int Errno >
static pid_t
pipe_fail(int* UTILS_UNUSED_PARAM(fildes)) throw()
{
    errno = Errno;
    return -1;
}


/// Helper for child_with_files tests to validate inheritance of stdout/stderr.
///
/// This function ensures that passing one of /dev/stdout or /dev/stderr to
/// the child_with_files fork method does the right thing.  The idea is that we
/// call fork with the given parameters and then make our child redirect one of
/// its file descriptors to a specific file without going through the process
/// library.  We then validate if this redirection worked and got the expected
/// output.
///
/// \param fork_stdout The path to pass to the fork call as the stdout file.
/// \param fork_stderr The path to pass to the fork call as the stderr file.
/// \param child_file The file to explicitly in the subchild.
/// \param child_fd The file descriptor to which to attach child_file.
static void
do_inherit_test(const char* fork_stdout, const char* fork_stderr,
                const char* child_file, const int child_fd)
{
    const pid_t pid = ::fork();
    ATF_REQUIRE(pid != -1);
    if (pid == 0) {
        logging::set_inmemory();

        const int fd = ::open(child_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd != child_fd) {
            if (::dup2(fd, child_fd) == -1)
                std::abort();
            ::close(fd);
        }

        std::auto_ptr< process::child_with_files > child =
            process::child_with_files::fork(
                child_simple_function< 123, 'Z' >,
                fs::path(fork_stdout), fs::path(fork_stderr));
        const process::status status = child->wait();
        if (!status.exited() || status.exitstatus() != 123)
            std::abort();
        std::exit(EXIT_SUCCESS);
    } else {
        int status;
        ATF_REQUIRE(::waitpid(pid, &status, 0) != -1);
        ATF_REQUIRE(WIFEXITED(status));
        ATF_REQUIRE_EQ(EXIT_SUCCESS, WEXITSTATUS(status));
        ATF_REQUIRE(atf::utils::grep_file("stdout: Z", "stdout.txt"));
        ATF_REQUIRE(atf::utils::grep_file("stderr: Z", "stderr.txt"));
    }
}


/// Performs a "child_with_output__ok_*" test.
///
/// This test basically ensures that the child_with_output class spawns a
/// process whose output is captured in an input stream.
///
/// \tparam Hook The type of the fork hook to use.
/// \param hook The hook to the fork call.
template< class Hook >
static void
child_with_output__ok(Hook hook)
{
    std::cout << "This unflushed message should not propagate to the child";
    std::cerr << "This unflushed message should not propagate to the child";
    std::auto_ptr< process::child_with_output > child =
        process::child_with_output::fork(hook);
    std::cout << std::endl;
    std::cerr << std::endl;

    std::istream& output = child->output();
    for (std::size_t i = 0; i < 100; i++) {
        std::string line;
        ATF_REQUIRE(std::getline(output, line).good());
        ATF_REQUIRE_EQ((F("This is a message to stdout, "
                          "sequence %s") % i).str(), line);
    }

    std::string line;
    ATF_REQUIRE(std::getline(output, line).good());
    ATF_REQUIRE_EQ("Exiting", line);

    process::status status = child->wait();
    ATF_REQUIRE(status.exited());
    ATF_REQUIRE_EQ(EXIT_SUCCESS, status.exitstatus());
}


}  // anonymous namespace


ATF_TEST_CASE_WITHOUT_HEAD(child_with_files__ok_function);
ATF_TEST_CASE_BODY(child_with_files__ok_function)
{
    const fs::path file1("file1.txt");
    const fs::path file2("file2.txt");

    std::auto_ptr< process::child_with_files > child =
        process::child_with_files::fork(
            child_simple_function< 15, 'Z' >, file1, file2);
    const process::status status = child->wait();
    ATF_REQUIRE(status.exited());
    ATF_REQUIRE_EQ(15, status.exitstatus());

    ATF_REQUIRE( atf::utils::grep_file("^To stdout: Z$", file1.str()));
    ATF_REQUIRE(!atf::utils::grep_file("^To stdout: Z$", file2.str()));

    ATF_REQUIRE( atf::utils::grep_file("^To stderr: Z$", file2.str()));
    ATF_REQUIRE(!atf::utils::grep_file("^To stderr: Z$", file1.str()));
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_files__ok_functor);
ATF_TEST_CASE_BODY(child_with_files__ok_functor)
{
    const fs::path filea("fileA.txt");
    const fs::path fileb("fileB.txt");

    {
        std::ofstream output(filea.c_str());
        output << "Initial stdout\n";
    }
    {
        std::ofstream output(fileb.c_str());
        output << "Initial stderr\n";
    }

    std::auto_ptr< process::child_with_files > child =
        process::child_with_files::fork(
            child_simple_functor(16, "a functor"), filea, fileb);
    const process::status status = child->wait();
    ATF_REQUIRE(status.exited());
    ATF_REQUIRE_EQ(16, status.exitstatus());

    ATF_REQUIRE( atf::utils::grep_file("^Initial stdout$", filea.str()));
    ATF_REQUIRE(!atf::utils::grep_file("^Initial stdout$", fileb.str()));

    ATF_REQUIRE( atf::utils::grep_file("^To stdout: a functor$", filea.str()));
    ATF_REQUIRE(!atf::utils::grep_file("^To stdout: a functor$", fileb.str()));

    ATF_REQUIRE( atf::utils::grep_file("^Initial stderr$", fileb.str()));
    ATF_REQUIRE(!atf::utils::grep_file("^Initial stderr$", filea.str()));

    ATF_REQUIRE( atf::utils::grep_file("^To stderr: a functor$", fileb.str()));
    ATF_REQUIRE(!atf::utils::grep_file("^To stderr: a functor$", filea.str()));
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_files__pid);
ATF_TEST_CASE_BODY(child_with_files__pid)
{
    std::auto_ptr< process::child_with_files > child =
        process::child_with_files::fork(
            child_write_pid, fs::path("file1.txt"), fs::path("file2.txt"));

    child_write_pid_check(child);
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_files__inherit_stdout);
ATF_TEST_CASE_BODY(child_with_files__inherit_stdout)
{
    do_inherit_test("/dev/stdout", "stderr.txt", "stdout.txt", STDOUT_FILENO);
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_files__inherit_stderr);
ATF_TEST_CASE_BODY(child_with_files__inherit_stderr)
{
    do_inherit_test("stdout.txt", "/dev/stderr", "stderr.txt", STDERR_FILENO);
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_files__wait_killpg);
ATF_TEST_CASE_BODY(child_with_files__wait_killpg)
{
    std::auto_ptr< process::child_with_files > child =
        process::child_with_files::fork(child_blocking_subchild,
                                        fs::path("out"), fs::path("err"));

    child_blocking_subchild_check(child);
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_files__wait_timeout_ok);
ATF_TEST_CASE_BODY(child_with_files__wait_timeout_ok)
{
    std::auto_ptr< process::child_with_files > child =
        process::child_with_files::fork(
            child_wait< 500000 >, fs::path("out"), fs::path("err"));
    const process::status status = child->wait(datetime::delta(5, 0));
    ATF_REQUIRE(fs::exists(fs::path("finished")));
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_files__wait_timeout_expired);
ATF_TEST_CASE_BODY(child_with_files__wait_timeout_expired)
{
    std::auto_ptr< process::child_with_files > child =
        process::child_with_files::fork(
            child_wait_with_subchild< 500000 >, fs::path("out"),
            fs::path("err"));
    ATF_REQUIRE_THROW(process::timeout_error,
                      child->wait(datetime::delta(0, 50000)));
    ATF_REQUIRE(!fs::exists(fs::path("finished")));

    // Check that the subprocess of the child is also killed.
    ::sleep(1);
    ATF_REQUIRE(!fs::exists(fs::path("finished")));
    ATF_REQUIRE(!fs::exists(fs::path("subfinished")));
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_files__interrupted);
ATF_TEST_CASE_BODY(child_with_files__interrupted)
{
    std::auto_ptr< process::child_with_files > child =
        process::child_with_files::fork(child_wait< 30000000 >,
                                        fs::path("out"), fs::path("err"));

    interrupted_check(child);
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_files__fork_cannot_exit);
ATF_TEST_CASE_BODY(child_with_files__fork_cannot_exit)
{
    const pid_t parent_pid = ::getpid();
    atf::utils::create_file("to-not-be-deleted", "");

    std::auto_ptr< process::child_with_files > child =
        process::child_with_files::fork(child_return,
                                        fs::path("out"), fs::path("err"));
    if (::getpid() != parent_pid) {
        // If we enter this clause, it is because the hook returned.
        ::unlink("to-not-be-deleted");
        std::exit(EXIT_SUCCESS);
    }

    const process::status status = child->wait();
    ATF_REQUIRE(status.signaled());
    ATF_REQUIRE(fs::exists(fs::path("to-not-be-deleted")));
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_files__fork_cannot_unwind);
ATF_TEST_CASE_BODY(child_with_files__fork_cannot_unwind)
{
    const pid_t parent_pid = ::getpid();
    atf::utils::create_file("to-not-be-deleted", "");
    try {
        std::auto_ptr< process::child_with_files > child =
            process::child_with_files::fork(child_raise_exception< int, 123 >,
                                            fs::path("out"), fs::path("err"));
        const process::status status = child->wait();
        ATF_REQUIRE(status.signaled());
        ATF_REQUIRE(fs::exists(fs::path("to-not-be-deleted")));
    } catch (const int i) {
        // If we enter this clause, it is because an exception leaked from the
        // hook.
        INV(parent_pid != ::getpid());
        INV(i == 123);
        ::unlink("to-not-be-deleted");
        std::exit(EXIT_SUCCESS);
    }
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_files__fork_fail);
ATF_TEST_CASE_BODY(child_with_files__fork_fail)
{
    process::detail::syscall_fork = fork_fail< 1234 >;
    try {
        process::child_with_files::fork(child_simple_function< 1, 'A' >,
                                        fs::path("a.txt"),
                                        fs::path("b.txt"));
        fail("Expected exception but none raised");
    } catch (const process::system_error& e) {
        ATF_REQUIRE(atf::utils::grep_string("fork.*failed", e.what()));
        ATF_REQUIRE_EQ(1234, e.original_errno());
    }
    ATF_REQUIRE(!fs::exists(fs::path("a.txt")));
    ATF_REQUIRE(!fs::exists(fs::path("b.txt")));
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_files__create_stdout_fail);
ATF_TEST_CASE_BODY(child_with_files__create_stdout_fail)
{
    process::detail::syscall_open = open_fail< ENOENT >;
    std::auto_ptr< process::child_with_files > child =
        process::child_with_files::fork(child_simple_function< 1, 'A' >,
                                        fs::path("raise-error"),
                                        fs::path("created"));
    const process::status status = child->wait();
    ATF_REQUIRE(status.signaled());
    ATF_REQUIRE_EQ(SIGABRT, status.termsig());
    ATF_REQUIRE(!fs::exists(fs::path("raise-error")));
    ATF_REQUIRE(!fs::exists(fs::path("created")));
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_files__create_stderr_fail);
ATF_TEST_CASE_BODY(child_with_files__create_stderr_fail)
{
    process::detail::syscall_open = open_fail< ENOENT >;
    std::auto_ptr< process::child_with_files > child =
        process::child_with_files::fork(child_simple_function< 1, 'A' >,
                                        fs::path("created"),
                                        fs::path("raise-error"));
    const process::status status = child->wait();
    ATF_REQUIRE(status.signaled());
    ATF_REQUIRE_EQ(SIGABRT, status.termsig());
    ATF_REQUIRE(fs::exists(fs::path("created")));
    ATF_REQUIRE(!fs::exists(fs::path("raise-error")));
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_output__ok_function);
ATF_TEST_CASE_BODY(child_with_output__ok_function)
{
    child_with_output__ok(child_printer_function);
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_output__ok_functor);
ATF_TEST_CASE_BODY(child_with_output__ok_functor)
{
    child_with_output__ok(child_printer_functor());
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_output__pid);
ATF_TEST_CASE_BODY(child_with_output__pid)
{
    std::auto_ptr< process::child_with_output > child =
        process::child_with_output::fork(child_write_pid);

    child_write_pid_check(child);
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_output__wait_killpg);
ATF_TEST_CASE_BODY(child_with_output__wait_killpg)
{
    std::auto_ptr< process::child_with_output > child =
        process::child_with_output::fork(child_blocking_subchild);
    child_blocking_subchild_check(child);
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_output__wait_timeout_ok);
ATF_TEST_CASE_BODY(child_with_output__wait_timeout_ok)
{
    std::auto_ptr< process::child_with_output > child =
        process::child_with_output::fork(child_wait< 500000 >);
    const process::status status = child->wait(datetime::delta(5, 0));
    ATF_REQUIRE(fs::exists(fs::path("finished")));
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_output__wait_timeout_expired);
ATF_TEST_CASE_BODY(child_with_output__wait_timeout_expired)
{
    std::auto_ptr< process::child_with_output > child =
        process::child_with_output::fork(child_wait_with_subchild< 500000 >);
    ATF_REQUIRE_THROW(process::timeout_error,
                      child->wait(datetime::delta(0, 50000)));
    ATF_REQUIRE(!fs::exists(fs::path("finished")));

    // Check that the subprocess of the child is also killed.
    ::sleep(1);
    ATF_REQUIRE(!fs::exists(fs::path("finished")));
    ATF_REQUIRE(!fs::exists(fs::path("subfinished")));
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_output__interrupted);
ATF_TEST_CASE_BODY(child_with_output__interrupted)
{
    std::auto_ptr< process::child_with_output > child =
        process::child_with_output::fork(child_wait< 30000000 >);

    interrupted_check(child);
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_output__pipe_fail);
ATF_TEST_CASE_BODY(child_with_output__pipe_fail)
{
    process::detail::syscall_pipe = pipe_fail< 23 >;
    try {
        process::child_with_output::fork(child_simple_function< 1, 'A' >);
        fail("Expected exception but none raised");
    } catch (const process::system_error& e) {
        ATF_REQUIRE(atf::utils::grep_string("pipe.*failed", e.what()));
        ATF_REQUIRE_EQ(23, e.original_errno());
    }
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_output__fork_cannot_exit);
ATF_TEST_CASE_BODY(child_with_output__fork_cannot_exit)
{
    const pid_t parent_pid = ::getpid();
    atf::utils::create_file("to-not-be-deleted", "");

    std::auto_ptr< process::child_with_output > child =
        process::child_with_output::fork(child_return);
    if (::getpid() != parent_pid) {
        // If we enter this clause, it is because the hook returned.
        ::unlink("to-not-be-deleted");
        std::exit(EXIT_SUCCESS);
    }

    const process::status status = child->wait();
    ATF_REQUIRE(status.signaled());
    ATF_REQUIRE(fs::exists(fs::path("to-not-be-deleted")));
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_output__fork_cannot_unwind);
ATF_TEST_CASE_BODY(child_with_output__fork_cannot_unwind)
{
    const pid_t parent_pid = ::getpid();
    atf::utils::create_file("to-not-be-deleted", "");
    try {
        std::auto_ptr< process::child_with_output > child =
            process::child_with_output::fork(child_raise_exception< int, 123 >);
        const process::status status = child->wait();
        ATF_REQUIRE(status.signaled());
        ATF_REQUIRE(fs::exists(fs::path("to-not-be-deleted")));
    } catch (const int i) {
        // If we enter this clause, it is because an exception leaked from the
        // hook.
        INV(parent_pid != ::getpid());
        INV(i == 123);
        ::unlink("to-not-be-deleted");
        std::exit(EXIT_SUCCESS);
    }
}


ATF_TEST_CASE_WITHOUT_HEAD(child_with_output__fork_fail);
ATF_TEST_CASE_BODY(child_with_output__fork_fail)
{
    process::detail::syscall_fork = fork_fail< 89 >;
    try {
        process::child_with_output::fork(child_simple_function< 1, 'A' >);
        fail("Expected exception but none raised");
    } catch (const process::system_error& e) {
        ATF_REQUIRE(atf::utils::grep_string("fork.*failed", e.what()));
        ATF_REQUIRE_EQ(89, e.original_errno());
    }
}


ATF_TEST_CASE_WITHOUT_HEAD(exec__absolute_path);
ATF_TEST_CASE_BODY(exec__absolute_path)
{
    std::vector< std::string > args;
    args.push_back("return-code");
    args.push_back("12");

    const fs::path program = get_helpers(this);
    INV(program.is_absolute());
    std::auto_ptr< process::child_with_files > child =
        process::child_with_files::fork(do_exec(program, args),
                                        fs::path("out"), fs::path("err"));

    const process::status status = child->wait();
    ATF_REQUIRE(status.exited());
    ATF_REQUIRE_EQ(12, status.exitstatus());
}


ATF_TEST_CASE_WITHOUT_HEAD(exec__relative_path);
ATF_TEST_CASE_BODY(exec__relative_path)
{
    std::vector< std::string > args;
    args.push_back("return-code");
    args.push_back("13");

    ATF_REQUIRE(::mkdir("root", 0755) != -1);
    ATF_REQUIRE(::symlink(get_helpers(this).c_str(), "root/helpers") != -1);

    std::auto_ptr< process::child_with_files > child =
        process::child_with_files::fork(do_exec(fs::path("root/helpers"), args),
                                        fs::path("out"), fs::path("err"));

    const process::status status = child->wait();
    ATF_REQUIRE(status.exited());
    ATF_REQUIRE_EQ(13, status.exitstatus());
}


ATF_TEST_CASE_WITHOUT_HEAD(exec__basename_only);
ATF_TEST_CASE_BODY(exec__basename_only)
{
    std::vector< std::string > args;
    args.push_back("return-code");
    args.push_back("14");

    ATF_REQUIRE(::symlink(get_helpers(this).c_str(), "helpers") != -1);

    std::auto_ptr< process::child_with_files > child =
        process::child_with_files::fork(do_exec(fs::path("helpers"), args),
                                        fs::path("out"), fs::path("err"));

    const process::status status = child->wait();
    ATF_REQUIRE(status.exited());
    ATF_REQUIRE_EQ(14, status.exitstatus());
}


ATF_TEST_CASE_WITHOUT_HEAD(exec__no_path);
ATF_TEST_CASE_BODY(exec__no_path)
{
    logging::set_inmemory();

    std::vector< std::string > args;
    args.push_back("return-code");
    args.push_back("14");

    const fs::path helpers = get_helpers(this);
    utils::setenv("PATH", helpers.branch_path().c_str());
    std::auto_ptr< process::child_with_output > child =
        process::child_with_output::fork(do_exec(fs::path(helpers.leaf_name()),
                                                 args));

    std::string line;
    ATF_REQUIRE(std::getline(child->output(), line).good());
    ATF_REQUIRE_MATCH("Failed to execute", line);
    ATF_REQUIRE(!std::getline(child->output(), line));

    const process::status status = child->wait();
    ATF_REQUIRE(status.signaled());
    ATF_REQUIRE_EQ(SIGABRT, status.termsig());
}


ATF_TEST_CASE_WITHOUT_HEAD(exec__no_args);
ATF_TEST_CASE_BODY(exec__no_args)
{
    std::vector< std::string > args;
    std::auto_ptr< process::child_with_output > child =
        process::child_with_output::fork(do_exec(get_helpers(this), args));

    std::string line;
    ATF_REQUIRE(std::getline(child->output(), line).good());
    ATF_REQUIRE_EQ("Must provide a helper name", line);
    ATF_REQUIRE(!std::getline(child->output(), line));

    const process::status status = child->wait();
    ATF_REQUIRE(status.exited());
    ATF_REQUIRE_EQ(EXIT_FAILURE, status.exitstatus());
}


ATF_TEST_CASE_WITHOUT_HEAD(exec__some_args);
ATF_TEST_CASE_BODY(exec__some_args)
{
    std::vector< std::string > args;
    args.push_back("print-args");
    args.push_back("foo");
    args.push_back("   bar baz ");
    std::auto_ptr< process::child_with_output > child =
        process::child_with_output::fork(do_exec(get_helpers(this), args));

    std::string line;
    ATF_REQUIRE(std::getline(child->output(), line).good());
    ATF_REQUIRE_EQ("argv[0] = " + get_helpers(this).str(), line);
    ATF_REQUIRE(std::getline(child->output(), line).good());
    ATF_REQUIRE_EQ("argv[1] = print-args", line);
    ATF_REQUIRE(std::getline(child->output(), line));
    ATF_REQUIRE_EQ("argv[2] = foo", line);
    ATF_REQUIRE(std::getline(child->output(), line));
    ATF_REQUIRE_EQ("argv[3] =    bar baz ", line);
    ATF_REQUIRE(std::getline(child->output(), line));
    ATF_REQUIRE_EQ("argv[4] = NULL", line);
    ATF_REQUIRE(!std::getline(child->output(), line));

    const process::status status = child->wait();
    ATF_REQUIRE(status.exited());
    ATF_REQUIRE_EQ(EXIT_SUCCESS, status.exitstatus());
}


ATF_TEST_CASE_WITHOUT_HEAD(exec__missing_program);
ATF_TEST_CASE_BODY(exec__missing_program)
{
    std::vector< std::string > args;
    std::auto_ptr< process::child_with_output > child =
        process::child_with_output::fork(do_exec(fs::path("a/b/c"), args));

    std::string line;
    ATF_REQUIRE(std::getline(child->output(), line).good());
    const std::string exp = "Caught system_error: Failed to execute a/b/c: ";
    ATF_REQUIRE_EQ(exp, line.substr(0, exp.length()));
    ATF_REQUIRE(!std::getline(child->output(), line));

    const process::status status = child->wait();
    ATF_REQUIRE(status.signaled());
    ATF_REQUIRE_EQ(SIGABRT, status.termsig());
}


ATF_INIT_TEST_CASES(tcs)
{
    ATF_ADD_TEST_CASE(tcs, child_with_files__ok_function);
    ATF_ADD_TEST_CASE(tcs, child_with_files__ok_functor);
    ATF_ADD_TEST_CASE(tcs, child_with_files__pid);
    ATF_ADD_TEST_CASE(tcs, child_with_files__inherit_stdout);
    ATF_ADD_TEST_CASE(tcs, child_with_files__inherit_stderr);
    ATF_ADD_TEST_CASE(tcs, child_with_files__wait_killpg);
    ATF_ADD_TEST_CASE(tcs, child_with_files__wait_timeout_ok);
    ATF_ADD_TEST_CASE(tcs, child_with_files__wait_timeout_expired);
    ATF_ADD_TEST_CASE(tcs, child_with_files__interrupted);
    ATF_ADD_TEST_CASE(tcs, child_with_files__fork_cannot_exit);
    ATF_ADD_TEST_CASE(tcs, child_with_files__fork_cannot_unwind);
    ATF_ADD_TEST_CASE(tcs, child_with_files__fork_fail);
    ATF_ADD_TEST_CASE(tcs, child_with_files__create_stdout_fail);
    ATF_ADD_TEST_CASE(tcs, child_with_files__create_stderr_fail);

    ATF_ADD_TEST_CASE(tcs, child_with_output__ok_function);
    ATF_ADD_TEST_CASE(tcs, child_with_output__ok_functor);
    ATF_ADD_TEST_CASE(tcs, child_with_output__pid);
    ATF_ADD_TEST_CASE(tcs, child_with_output__wait_killpg);
    ATF_ADD_TEST_CASE(tcs, child_with_output__wait_timeout_ok);
    ATF_ADD_TEST_CASE(tcs, child_with_output__wait_timeout_expired);
    ATF_ADD_TEST_CASE(tcs, child_with_output__interrupted);
    ATF_ADD_TEST_CASE(tcs, child_with_output__pipe_fail);
    ATF_ADD_TEST_CASE(tcs, child_with_output__fork_cannot_exit);
    ATF_ADD_TEST_CASE(tcs, child_with_output__fork_cannot_unwind);
    ATF_ADD_TEST_CASE(tcs, child_with_output__fork_fail);

    ATF_ADD_TEST_CASE(tcs, exec__absolute_path);
    ATF_ADD_TEST_CASE(tcs, exec__relative_path);
    ATF_ADD_TEST_CASE(tcs, exec__basename_only);
    ATF_ADD_TEST_CASE(tcs, exec__no_path);
    ATF_ADD_TEST_CASE(tcs, exec__no_args);
    ATF_ADD_TEST_CASE(tcs, exec__some_args);
    ATF_ADD_TEST_CASE(tcs, exec__missing_program);
}
