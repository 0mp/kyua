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

#include <atf-c++.hpp>

#include "utils/env.hpp"
#include "utils/optional.ipp"

using utils::optional;


ATF_TEST_CASE_WITHOUT_HEAD(getenv);
ATF_TEST_CASE_BODY(getenv)
{
    const optional< std::string > path = utils::getenv("PATH");
    ATF_REQUIRE(path);
    ATF_REQUIRE(!path.get().empty());

    ATF_REQUIRE(!utils::getenv("__UNDEFINED_VARIABLE__"));
}


ATF_TEST_CASE_WITHOUT_HEAD(setenv);
ATF_TEST_CASE_BODY(setenv)
{
    ATF_REQUIRE(utils::getenv("PATH"));
    const std::string oldval = utils::getenv("PATH").get();
    utils::setenv("PATH", "foo-bar");
    ATF_REQUIRE(utils::getenv("PATH").get() != oldval);
    ATF_REQUIRE_EQ("foo-bar", utils::getenv("PATH").get());

    ATF_REQUIRE(!utils::getenv("__UNDEFINED_VARIABLE__"));
    utils::setenv("__UNDEFINED_VARIABLE__", "foo2-bar2");
    ATF_REQUIRE_EQ("foo2-bar2", utils::getenv("__UNDEFINED_VARIABLE__").get());
}


ATF_TEST_CASE_WITHOUT_HEAD(unsetenv);
ATF_TEST_CASE_BODY(unsetenv)
{
    ATF_REQUIRE(utils::getenv("PATH"));
    utils::unsetenv("PATH");
    ATF_REQUIRE(!utils::getenv("PATH"));
}


ATF_INIT_TEST_CASES(tcs)
{
    ATF_ADD_TEST_CASE(tcs, getenv);
    ATF_ADD_TEST_CASE(tcs, setenv);
    ATF_ADD_TEST_CASE(tcs, unsetenv);
}
