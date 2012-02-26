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

#include <iostream>

#include <atf-c++.hpp>

#include "utils/signals/timer.hpp"

namespace signals = utils::signals;


namespace {


static volatile bool fired;


static void
callback(void)
{
    ::fired = true;
}


}  // anonymous namespace


ATF_TEST_CASE_WITHOUT_HEAD(timedelta__defaults);
ATF_TEST_CASE_BODY(timedelta__defaults)
{
    const signals::timedelta delta;
    ATF_REQUIRE_EQ(0, delta.seconds);
    ATF_REQUIRE_EQ(0, delta.useconds);
}


ATF_TEST_CASE_WITHOUT_HEAD(timedelta__overrides);
ATF_TEST_CASE_BODY(timedelta__overrides)
{
    const signals::timedelta delta(1, 2);
    ATF_REQUIRE_EQ(1, delta.seconds);
    ATF_REQUIRE_EQ(2, delta.useconds);
}


ATF_TEST_CASE_WITHOUT_HEAD(timedelta__equals);
ATF_TEST_CASE_BODY(timedelta__equals)
{
    using signals::timedelta;

    ATF_REQUIRE(timedelta() == timedelta());
    ATF_REQUIRE(timedelta() == timedelta(0, 0));
    ATF_REQUIRE(timedelta(1, 2) == timedelta(1, 2));

    ATF_REQUIRE(!(timedelta() == timedelta(0, 1)));
    ATF_REQUIRE(!(timedelta() == timedelta(1, 0)));
    ATF_REQUIRE(!(timedelta(1, 2) == timedelta(2, 1)));
}


ATF_TEST_CASE(timer__program_seconds);
ATF_TEST_CASE_HEAD(timer__program_seconds)
{
    set_md_var("timeout", "10");
}
ATF_TEST_CASE_BODY(timer__program_seconds)
{
    ::fired = false;
    signals::timer timer(signals::timedelta(1, 0), callback);
    ATF_REQUIRE(!::fired);
    while (!::fired)
        ::usleep(1000);
}


ATF_TEST_CASE(timer__program_useconds);
ATF_TEST_CASE_HEAD(timer__program_useconds)
{
    set_md_var("timeout", "10");
}
ATF_TEST_CASE_BODY(timer__program_useconds)
{
    ::fired = false;
    signals::timer timer(signals::timedelta(0, 500000), callback);
    while (!::fired)
        ::usleep(1000);
}


ATF_TEST_CASE(timer__unprogram);
ATF_TEST_CASE_HEAD(timer__unprogram)
{
    set_md_var("timeout", "10");
}
ATF_TEST_CASE_BODY(timer__unprogram)
{
    ::fired = false;
    signals::timer timer(signals::timedelta(0, 500000), callback);
    timer.unprogram();
    usleep(500000);
    ATF_REQUIRE(!::fired);
}


ATF_TEST_CASE(timer__infinitesimal);
ATF_TEST_CASE_HEAD(timer__infinitesimal)
{
    set_md_var("descr", "Ensure that the ordering in which the signal, the "
               "timer and the global state are programmed is correct; do so "
               "by setting an extremely small delay for the timer hoping that "
               "it can trigger such conditions");
    set_md_var("timeout", "10");
}
ATF_TEST_CASE_BODY(timer__infinitesimal)
{
    for (int i = 0; i < 100; i++) {
        std::cout << "In attempt " << i << "\n";

        ::fired = false;
        signals::timer timer(signals::timedelta(0, 1), callback);

        // From the setitimer(2) documentation:
        //
        //     Time values smaller than the resolution of the system clock are
        //     rounded up to this resolution (typically 10 milliseconds).
        //
        // We don't know what this resolution is but we must wait for longer
        // than we programmed; do a rough guess and hope it is good.  This may
        // be obviously wrong and thus lead to mysterious test failures in some
        // systems.  You have been told.
        ::usleep(100);

        ATF_REQUIRE(::fired);
        timer.unprogram();
    }
}


ATF_INIT_TEST_CASES(tcs)
{
    ATF_ADD_TEST_CASE(tcs, timedelta__defaults);
    ATF_ADD_TEST_CASE(tcs, timedelta__overrides);

    ATF_ADD_TEST_CASE(tcs, timer__program_seconds);
    ATF_ADD_TEST_CASE(tcs, timer__program_useconds);
    ATF_ADD_TEST_CASE(tcs, timer__unprogram);
    ATF_ADD_TEST_CASE(tcs, timer__infinitesimal);
}
