// Copyright 2011 Google Inc.
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

#include "engine/isolation.ipp"

extern "C" {
#include <sys/stat.h>

#include <signal.h>
#include <unistd.h>
}

#include <cstdlib>
#include <fstream>
#include <iostream>

#include <atf-c++.hpp>

#include "utils/env.hpp"
#include "utils/format/macros.hpp"
#include "utils/fs/operations.hpp"
#include "utils/logging/macros.hpp"
#include "utils/process/children.hpp"
#include "utils/sanity.hpp"
#include "utils/signals/misc.hpp"

namespace datetime = utils::datetime;
namespace fs = utils::fs;
namespace process = utils::process;
namespace signals = utils::signals;

using utils::optional;


namespace {


/// Body for a subprocess that prints messages and exits.
void
fork_and_wait_hook_ok(void)
{
    std::cout << "stdout message\n";
    std::cerr << "stderr message\n";
    std::exit(32);
}


/// Body for a subprocess that gets stuck.
///
/// This attempts to configure all signals to be ignored so that the caller
/// process has to kill this child by sending an uncatchable signal.
void
fork_and_wait_hook_block(void)
{
    for (int i = 0; i <= signals::last_signo; i++) {
        struct ::sigaction sa;
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        if (::sigaction(i, &sa, NULL) == -1)
            LD(F("Failed to ignore signal %s (may be normal!)") % i);
        else
            LD(F("Ignoring signal %s") % i);
    }

    for (;;)
        ::pause();
}


/// Hook for protected_run() that validates the value of the work directory.
///
/// The caller needs to arrange for a mechanism to make the work directory
/// parameter passed to the hook to include a particular known value.  This can
/// be done, for example, by setting TMPDIR.
class protected_run_hook_check_workdir {
    /// The dirname of the work directory to expect.
    const fs::path _dirname;

    /// The test result to return.
    const engine::test_result _result;

public:
    /// Constructs a new functor.
    ///
    /// \param dirname_ The dirname of the expected work directory.
    /// \param result_ The test result to return from the hook.
    protected_run_hook_check_workdir(const char* dirname_,
                                     const engine::test_result& result_) :
        _dirname(dirname_),
        _result(result_)
    {
    }

    /// Runs the functor.
    ///
    /// \param workdir The work directory calculated by protected_run().  Its
    ///     dirname must match what we expect as defined during construction.
    ///
    /// \return The test result passed to the constructor.
    const engine::test_result&
    operator()(const fs::path& workdir)
    {
        ATF_REQUIRE_EQ(_dirname, workdir.branch_path());
        return _result;
    }
};


/// Hook for protected_run() that makes the work directory unwritable.
class protected_run_hook_protect {
    /// The test result to return.
    const engine::test_result _result;

public:
    /// Constructs a new functor.
    ///
    /// \param result_ The test result to return from the hook.
    protected_run_hook_protect(const engine::test_result& result_) :
        _result(result_)
    {
    }

    /// Runs the functor.
    ///
    /// \param workdir The work directory calculated by protected_run().
    ///
    /// \return The test result passed to the constructor.
    const engine::test_result&
    operator()(const fs::path& workdir)
    {
        ::chmod(workdir.branch_path().c_str(), 0555);
        return _result;
    }
};


/// Hook for protected_run() that dies during execution.
class protected_run_hook_signal {
    /// The signal to send to ourselves to commit suicide.
    const int _signo;

public:
    /// Constructs a new functor.
    ///
    /// \param signo_ The signal to send to ourselves to commit suicide.
    ///     Note that protected_run() does NOT spawn a subprocess, so the signal
    ///     passed here must be catchable.
    protected_run_hook_signal(const int signo_) :
        _signo(signo_)
    {
    }

    /// Runs the functor.
    ///
    /// \param unused_workdir The work directory calculated by protected_run().
    ///
    /// \return A passed test case result.  This should not happen though, as
    /// the signal raised before returning should kill ourselves.
    engine::test_result
    operator()(const fs::path& UTILS_UNUSED_PARAM(workdir))
    {
        ::kill(::getpid(), _signo);
        return engine::test_result(engine::test_result::passed);
    }
};


}  // anonymous namespace


ATF_TEST_CASE_WITHOUT_HEAD(create_work_directory__hardcoded);
ATF_TEST_CASE_BODY(create_work_directory__hardcoded)
{
    utils::unsetenv("TMPDIR");
    const fs::path workdir(engine::create_work_directory());
    ATF_REQUIRE(::rmdir(workdir.c_str()) != -1);
    ATF_REQUIRE_EQ(fs::path("/tmp"), workdir.branch_path());
}


ATF_TEST_CASE_WITHOUT_HEAD(create_work_directory__tmpdir);
ATF_TEST_CASE_BODY(create_work_directory__tmpdir)
{
    utils::setenv("TMPDIR", ".");
    const fs::path workdir(engine::create_work_directory());
    ATF_REQUIRE(::rmdir(workdir.c_str()) != -1);
    ATF_REQUIRE_EQ(fs::path("."), workdir.branch_path());
    ATF_REQUIRE_EQ("kyua.", workdir.leaf_name().substr(0, 5));
}


ATF_TEST_CASE_WITHOUT_HEAD(protected_run__ok);
ATF_TEST_CASE_BODY(protected_run__ok)
{
    ATF_REQUIRE(::mkdir("my-tmpdir", 0755) != -1);
    utils::setenv("TMPDIR", "my-tmpdir");

    const engine::test_result result(engine::test_result::skipped, "Foo");
    const protected_run_hook_check_workdir hook("my-tmpdir", result);
    ATF_REQUIRE(result == engine::protected_run(hook));
}


ATF_TEST_CASE(protected_run__ok_but_cleanup_fail);
ATF_TEST_CASE_HEAD(protected_run__ok_but_cleanup_fail)
{
    set_md_var("require.user", "unprivileged");
}
ATF_TEST_CASE_BODY(protected_run__ok_but_cleanup_fail)
{
    ATF_REQUIRE(::mkdir("my-tmpdir", 0755) != -1);
    utils::setenv("TMPDIR", "my-tmpdir");

    const engine::test_result result(engine::test_result::broken, "Bar");
    const protected_run_hook_protect hook(result);
    ATF_REQUIRE(result == engine::protected_run(hook));
}


ATF_TEST_CASE(protected_run__fail_and_cleanup_fail);
ATF_TEST_CASE_HEAD(protected_run__fail_and_cleanup_fail)
{
    set_md_var("require.user", "unprivileged");
}
ATF_TEST_CASE_BODY(protected_run__fail_and_cleanup_fail)
{
    ATF_REQUIRE(::mkdir("my-tmpdir", 0755) != -1);
    utils::setenv("TMPDIR", "my-tmpdir");

    const engine::test_result result(engine::test_result::failed, "Oh no");
    const protected_run_hook_protect hook(result);
    ATF_REQUIRE(result == engine::protected_run(hook));
}


ATF_TEST_CASE_WITHOUT_HEAD(protected_run__interrupted);
ATF_TEST_CASE_BODY(protected_run__interrupted)
{
    const int signos[] = {SIGHUP, SIGINT, SIGTERM, -1};
    for (const int* signo = signos; *signo != -1; signo++) {
        ATF_REQUIRE(::mkdir("my-tmpdir", 0755) != -1);
        utils::setenv("TMPDIR", "my-tmpdir");

        ATF_REQUIRE_THROW(
            engine::interrupted_error,
            engine::protected_run(protected_run_hook_signal(*signo)));
        if (::rmdir("my-tmpdir") == -1)
            ATF_FAIL("Signal caught but work directory not cleaned");
    }
}


ATF_INIT_TEST_CASES(tcs)
{
    ATF_ADD_TEST_CASE(tcs, create_work_directory__hardcoded);
    ATF_ADD_TEST_CASE(tcs, create_work_directory__tmpdir);

    ATF_ADD_TEST_CASE(tcs, protected_run__ok);
    ATF_ADD_TEST_CASE(tcs, protected_run__ok_but_cleanup_fail);
    ATF_ADD_TEST_CASE(tcs, protected_run__fail_and_cleanup_fail);
    ATF_ADD_TEST_CASE(tcs, protected_run__interrupted);
}
