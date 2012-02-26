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

// TODO(jmmv): These tests ought to be written in Lua.  Rewrite when we have a
// Lua binding.

#include <fstream>

#include <atf-c++.hpp>
#include <lutok/exceptions.hpp>
#include <lutok/operations.hpp>
#include <lutok/wrap.hpp>

#include "engine/user_files/common.hpp"
#include "utils/fs/path.hpp"

namespace fs = utils::fs;
namespace user_files = engine::user_files;


ATF_TEST_CASE_WITHOUT_HEAD(empty);
ATF_TEST_CASE_BODY(empty)
{
    std::ofstream output("test.lua");
    ATF_REQUIRE(output);
    output << "syntax('config', 1)\n";
    output.close();

    lutok::state state;
    user_files::do_user_file(state, fs::path("test.lua"));
}


ATF_TEST_CASE_WITHOUT_HEAD(some_variables);
ATF_TEST_CASE_BODY(some_variables)
{
    std::ofstream output("test.lua");
    ATF_REQUIRE(output);
    output << "syntax('config', 1)\n";
    output << "foo = 'bar'\n";
    output << "baz = 3\n";
    output.close();

    lutok::state state;
    user_files::do_user_file(state, fs::path("test.lua"));
    lutok::do_string(state, "assert(foo == 'bar')");
    lutok::do_string(state, "assert(baz == 3)");
}


ATF_TEST_CASE_WITHOUT_HEAD(test_suites__ok);
ATF_TEST_CASE_BODY(test_suites__ok)
{
    std::ofstream output("test.lua");
    ATF_REQUIRE(output);
    output << "syntax('config', 1)\n";
    output << "test_suites.ts1.foo = 'bar'\n";
    output << "test_suites.ts1.foo = 'baz'\n";
    output << "test_suites.ts1.hello = 3\n";
    output << "test_suites.ts2.hello = 5\n";
    output << "test_suites.ts2.bye = true\n";
    output.close();

    lutok::state state;
    user_files::do_user_file(state, fs::path("test.lua"));
    lutok::do_string(state, "assert(config.TEST_SUITES.ts1.foo == 'baz')");
    lutok::do_string(state, "assert(config.TEST_SUITES.ts1.hello == 3)");
    lutok::do_string(state, "assert(config.TEST_SUITES.ts2.hello == 5)");
    lutok::do_string(state, "assert(config.TEST_SUITES.ts2.bye == true)");
}


ATF_TEST_CASE_WITHOUT_HEAD(test_suites__get__invalid_key_type);
ATF_TEST_CASE_BODY(test_suites__get__invalid_key_type)
{
    std::ofstream output("test.lua");
    ATF_REQUIRE(output);
    output << "syntax('config', 1)\n";
    output << "test_suites[3].foo = 'abc'\n";
    output.close();

    lutok::state state;
    ATF_REQUIRE_THROW_RE(lutok::error, "name must be a string",
                         user_files::do_user_file(state, fs::path("test.lua")));
}


ATF_TEST_CASE_WITHOUT_HEAD(test_suites__set__disallow);
ATF_TEST_CASE_BODY(test_suites__set__disallow)
{
    std::ofstream output("test.lua");
    ATF_REQUIRE(output);
    output << "syntax('config', 1)\n";
    output << "test_suites.hello = 'abc'\n";
    output.close();

    lutok::state state;
    ATF_REQUIRE_THROW_RE(lutok::error, "Cannot directly set.*test_suites",
                         user_files::do_user_file(state, fs::path("test.lua")));
}


ATF_TEST_CASE_WITHOUT_HEAD(test_suite__set__invalid_key_type);
ATF_TEST_CASE_BODY(test_suite__set__invalid_key_type)
{
    std::ofstream output("test.lua");
    ATF_REQUIRE(output);
    output << "syntax('config', 1)\n";
    output << "test_suites.hello[3] = {}\n";
    output.close();

    lutok::state state;
    ATF_REQUIRE_THROW_RE(lutok::error, "Key '3'.*not a string.*suite 'hello'",
                         user_files::do_user_file(state, fs::path("test.lua")));
}


ATF_TEST_CASE_WITHOUT_HEAD(test_suite__set__invalid_value_type);
ATF_TEST_CASE_BODY(test_suite__set__invalid_value_type)
{
    std::ofstream output("test.lua");
    ATF_REQUIRE(output);
    output << "syntax('config', 1)\n";
    output << "test_suites.hello.world = {}\n";
    output.close();

    lutok::state state;
    ATF_REQUIRE_THROW_RE(lutok::error, "Invalid type.*'world'.*suite 'hello'",
                         user_files::do_user_file(state, fs::path("test.lua")));
}


ATF_INIT_TEST_CASES(tcs)
{
    ATF_ADD_TEST_CASE(tcs, empty);
    ATF_ADD_TEST_CASE(tcs, some_variables);

    ATF_ADD_TEST_CASE(tcs, test_suites__ok);
    ATF_ADD_TEST_CASE(tcs, test_suites__get__invalid_key_type);
    ATF_ADD_TEST_CASE(tcs, test_suites__set__disallow);

    ATF_ADD_TEST_CASE(tcs, test_suite__set__invalid_key_type);
    ATF_ADD_TEST_CASE(tcs, test_suite__set__invalid_value_type);
}
