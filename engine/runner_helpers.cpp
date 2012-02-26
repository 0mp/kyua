// Copyright 2010, Google Inc.
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

extern "C" {
#include <sys/stat.h>

#include <signal.h>
#include <unistd.h>
}

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include <atf-c++.hpp>

#include "utils/env.hpp"
#include "utils/fs/operations.hpp"
#include "utils/fs/path.hpp"
#include "utils/optional.ipp"

namespace fs = utils::fs;


ATF_TEST_CASE_WITH_CLEANUP(check_cleanup_workdir);
ATF_TEST_CASE_HEAD(check_cleanup_workdir)
{
    set_md_var("require.config", "control_dir");
}
ATF_TEST_CASE_BODY(check_cleanup_workdir)
{
    std::ofstream cookie("workdir_cookie");
    cookie << "1234\n";
    cookie.close();
    skip("cookie created");
}
ATF_TEST_CASE_CLEANUP(check_cleanup_workdir)
{
    const fs::path control_dir(get_config_var("control_dir"));

    std::ifstream cookie("workdir_cookie");
    if (!cookie) {
        std::ofstream((control_dir / "missing_cookie").c_str()).close();
        std::exit(EXIT_FAILURE);
    }

    std::string value;
    cookie >> value;
    if (value != "1234") {
        std::ofstream((control_dir / "invalid_cookie").c_str()).close();
        std::exit(EXIT_FAILURE);
    }

    std::ofstream((control_dir / "cookie_ok").c_str()).close();
    std::exit(EXIT_SUCCESS);
}


ATF_TEST_CASE_WITHOUT_HEAD(crash);
ATF_TEST_CASE_BODY(crash)
{
    std::abort();
}


ATF_TEST_CASE_WITHOUT_HEAD(create_cookie_in_control_dir);
ATF_TEST_CASE_BODY(create_cookie_in_control_dir)
{
    if (!has_config_var("control_dir"))
        fail("control_dir not provided");

    const fs::path control_dir(get_config_var("control_dir"));
    std::ofstream file((control_dir / "cookie").c_str());
    if (!file)
        fail("Failed to create the control cookie");
    file.close();
}


ATF_TEST_CASE_WITHOUT_HEAD(create_cookie_in_workdir);
ATF_TEST_CASE_BODY(create_cookie_in_workdir)
{
    std::ofstream file("cookie");
    if (!file)
        fail("Failed to create the cookie");
    file.close();
}


ATF_TEST_CASE_WITHOUT_HEAD(pass);
ATF_TEST_CASE_BODY(pass)
{
}


ATF_TEST_CASE_WITHOUT_HEAD(validate_env);
ATF_TEST_CASE_BODY(validate_env)
{
    ATF_REQUIRE(utils::getenv("HOME").get() == fs::current_path().str());
    ATF_REQUIRE(!utils::getenv("LANG"));
    ATF_REQUIRE(!utils::getenv("LC_ALL"));
    ATF_REQUIRE(!utils::getenv("LC_COLLATE"));
    ATF_REQUIRE(!utils::getenv("LC_CTYPE"));
    ATF_REQUIRE(!utils::getenv("LC_MESSAGES"));
    ATF_REQUIRE(!utils::getenv("LC_MONETARY"));
    ATF_REQUIRE(!utils::getenv("LC_NUMERIC"));
    ATF_REQUIRE(!utils::getenv("LC_TIME"));
    ATF_REQUIRE(!utils::getenv("TZ"));
}


ATF_TEST_CASE_WITHOUT_HEAD(validate_pgrp);
ATF_TEST_CASE_BODY(validate_pgrp)
{
    if (::getpgrp() != ::getpid())
        fail("Test case not running in its own process group");
}


ATF_TEST_CASE(validate_signal);
ATF_TEST_CASE_HEAD(validate_signal)
{
    set_md_var("require.config", "signo");
}
ATF_TEST_CASE_BODY(validate_signal)
{
    std::istringstream iss(get_config_var("signo"));
    int signo;
    iss >> signo;
    std::cout << "Delivering signal " << signo << "\n";
    ::kill(::getpid(), signo);
}


ATF_TEST_CASE_WITHOUT_HEAD(validate_umask);
ATF_TEST_CASE_BODY(validate_umask)
{
    const mode_t old_umask = ::umask(0111);
    if (old_umask != 0022)
        fail("umask not set to 0022 when running test case");
}


ATF_INIT_TEST_CASES(tcs)
{
    ATF_ADD_TEST_CASE(tcs, check_cleanup_workdir);
    ATF_ADD_TEST_CASE(tcs, crash);
    ATF_ADD_TEST_CASE(tcs, create_cookie_in_control_dir);
    ATF_ADD_TEST_CASE(tcs, create_cookie_in_workdir);
    ATF_ADD_TEST_CASE(tcs, pass);
    ATF_ADD_TEST_CASE(tcs, validate_env);
    ATF_ADD_TEST_CASE(tcs, validate_pgrp);
    ATF_ADD_TEST_CASE(tcs, validate_signal);
    ATF_ADD_TEST_CASE(tcs, validate_umask);
}
