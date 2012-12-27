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

#include "engine/atf_iface/results.hpp"

extern "C" {
#include <signal.h>
}

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <atf-c++.hpp>

#include "engine/atf_iface/test_case.hpp"
#include "engine/exceptions.hpp"
#include "engine/test_result.hpp"
#include "utils/format/macros.hpp"
#include "utils/fs/path.hpp"
#include "utils/process/status.hpp"

namespace atf_iface = engine::atf_iface;
namespace datetime = utils::datetime;
namespace fs = utils::fs;
namespace process = utils::process;

using atf_iface::detail::raw_result;
using utils::none;
using utils::optional;


namespace {


/// Performs a test for results::parse() that should succeed.
///
/// \param exp_type The expected type of the result.
/// \param exp_argument The expected argument in the result, if any.
/// \param exp_reason The expected reason describing the result, if any.
/// \param text The literal input to parse; can include multiple lines.
static void
parse_ok_test(const raw_result::types& exp_type,
              const optional< int >& exp_argument,
              const char* exp_reason, const char* text)
{
    std::istringstream input(text);
    const raw_result actual = raw_result::parse(input);
    ATF_REQUIRE(exp_type == actual.type());
    ATF_REQUIRE(exp_argument == actual.argument());
    if (exp_reason != NULL) {
        ATF_REQUIRE(actual.reason());
        ATF_REQUIRE_EQ(exp_reason, actual.reason().get());
    } else {
        ATF_REQUIRE(!actual.reason());
    }
}


/// Wrapper around parse_ok_test to define a test case.
///
/// \param name The name of the test case; will be prefixed with
///     "raw_result__parse__".
/// \param exp_type The expected type of the result.
/// \param exp_argument The expected argument in the result, if any.
/// \param exp_reason The expected reason describing the result, if any.
/// \param input The literal input to parse.
#define PARSE_OK(name, exp_type, exp_argument, exp_reason, input) \
    ATF_TEST_CASE_WITHOUT_HEAD(raw_result__parse__ ## name); \
    ATF_TEST_CASE_BODY(raw_result__parse__ ## name) \
    { \
        parse_ok_test(exp_type, exp_argument, exp_reason, input); \
    }


/// Performs a test for results::parse() that should fail.
///
/// \param reason_regexp The reason to match against the broken reason.
/// \param text The literal input to parse; can include multiple lines.
static void
parse_broken_test(const char* reason_regexp, const char* text)
{
    std::istringstream input(text);
    ATF_REQUIRE_THROW_RE(engine::format_error, reason_regexp,
                         raw_result::parse(input));
}


/// Wrapper around parse_broken_test to define a test case.
///
/// \param name The name of the test case; will be prefixed with
///    "raw_result__parse__".
/// \param reason_regexp The reason to match against the broken reason.
/// \param input The literal input to parse.
#define PARSE_BROKEN(name, reason_regexp, input) \
    ATF_TEST_CASE_WITHOUT_HEAD(raw_result__parse__ ## name); \
    ATF_TEST_CASE_BODY(raw_result__parse__ ## name) \
    { \
        parse_broken_test(reason_regexp, input); \
    }


}  // anonymous namespace


PARSE_BROKEN(empty,
             "Empty.*no new line",
             "");
PARSE_BROKEN(no_newline__unknown,
             "Empty.*no new line",
             "foo");
PARSE_BROKEN(no_newline__known,
             "Empty.*no new line",
             "passed");
PARSE_BROKEN(multiline__no_newline,
             "multiple lines.*foo<<NEWLINE>>bar",
             "failed: foo\nbar");
PARSE_BROKEN(multiline__with_newline,
             "multiple lines.*foo<<NEWLINE>>bar",
             "failed: foo\nbar\n");
PARSE_BROKEN(unknown_status__no_reason,
             "Unknown.*result.*'cba'",
             "cba\n");
PARSE_BROKEN(unknown_status__with_reason,
             "Unknown.*result.*'hgf'",
             "hgf: foo\n");
PARSE_BROKEN(missing_reason__no_delim,
             "failed.*followed by.*reason",
             "failed\n");
PARSE_BROKEN(missing_reason__bad_delim,
             "failed.*followed by.*reason",
             "failed:\n");
PARSE_BROKEN(missing_reason__empty,
             "failed.*followed by.*reason",
             "failed: \n");


PARSE_OK(broken__ok,
         raw_result::broken, none, "a b c",
         "broken: a b c\n");
PARSE_OK(broken__blanks,
         raw_result::broken, none, "   ",
         "broken:    \n");


PARSE_OK(expected_death__ok,
         raw_result::expected_death, none, "a b c",
         "expected_death: a b c\n");
PARSE_OK(expected_death__blanks,
         raw_result::expected_death, none, "   ",
         "expected_death:    \n");


PARSE_OK(expected_exit__ok__any,
         raw_result::expected_exit, none, "any exit code",
         "expected_exit: any exit code\n");
PARSE_OK(expected_exit__ok__specific,
         raw_result::expected_exit, optional< int >(712),
         "some known exit code",
         "expected_exit(712): some known exit code\n");
PARSE_BROKEN(expected_exit__bad_int,
             "Invalid integer.*45a3",
             "expected_exit(45a3): this is broken\n");


PARSE_OK(expected_failure__ok,
         raw_result::expected_failure, none, "a b c",
         "expected_failure: a b c\n");
PARSE_OK(expected_failure__blanks,
         raw_result::expected_failure, none, "   ",
         "expected_failure:    \n");


PARSE_OK(expected_signal__ok__any,
         raw_result::expected_signal, none, "any signal code",
         "expected_signal: any signal code\n");
PARSE_OK(expected_signal__ok__specific,
         raw_result::expected_signal, optional< int >(712),
         "some known signal code",
         "expected_signal(712): some known signal code\n");
PARSE_BROKEN(expected_signal__bad_int,
             "Invalid integer.*45a3",
             "expected_signal(45a3): this is broken\n");


PARSE_OK(expected_timeout__ok,
         raw_result::expected_timeout, none, "a b c",
         "expected_timeout: a b c\n");
PARSE_OK(expected_timeout__blanks,
         raw_result::expected_timeout, none, "   ",
         "expected_timeout:    \n");


PARSE_OK(failed__ok,
         raw_result::failed, none, "a b c",
         "failed: a b c\n");
PARSE_OK(failed__blanks,
         raw_result::failed, none, "   ",
         "failed:    \n");


PARSE_OK(passed__ok,
         raw_result::passed, none, NULL,
         "passed\n");
PARSE_BROKEN(passed__reason,
             "cannot have a reason",
             "passed a b c\n");


PARSE_OK(skipped__ok,
         raw_result::skipped, none, "a b c",
         "skipped: a b c\n");
PARSE_OK(skipped__blanks,
         raw_result::skipped, none, "   ",
         "skipped:    \n");


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__load__ok);
ATF_TEST_CASE_BODY(raw_result__load__ok)
{
    std::ofstream output("result.txt");
    ATF_REQUIRE(output);
    output << "skipped: a b c\n";
    output.close();

    const raw_result result = raw_result::load(utils::fs::path("result.txt"));
    try {
        ATF_REQUIRE(raw_result::skipped == result.type());
        ATF_REQUIRE(!result.argument());
        ATF_REQUIRE(result.reason());
        ATF_REQUIRE_EQ("a b c", result.reason().get());
    } catch (const std::bad_cast* e) {
        fail("Invalid result type returned");
    }
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__load__missing_file);
ATF_TEST_CASE_BODY(raw_result__load__missing_file)
{
    ATF_REQUIRE_THROW_RE(std::runtime_error, "Cannot open",
                         raw_result::load(utils::fs::path("result.txt")));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__load__format_error);
ATF_TEST_CASE_BODY(raw_result__load__format_error)
{
    std::ofstream output("abc.txt");
    ATF_REQUIRE(output);
    output << "passed: foo\n";
    output.close();

    ATF_REQUIRE_THROW_RE(engine::format_error, "cannot have a reason",
                         raw_result::load(utils::fs::path("abc.txt")));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__broken__ok);
ATF_TEST_CASE_BODY(raw_result__apply__broken__ok)
{
    const raw_result in_result(raw_result::broken, "Passthrough");
    const process::status status = process::status::fake_exited(EXIT_SUCCESS);
    ATF_REQUIRE(in_result == in_result.apply(utils::make_optional(status)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__timed_out);
ATF_TEST_CASE_BODY(raw_result__apply__timed_out)
{
    const raw_result timed_out(raw_result::broken, "Some arbitrary error");
    ATF_REQUIRE(raw_result(raw_result::broken, "Test case body timed out") ==
                timed_out.apply(none));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__expected_death__ok);
ATF_TEST_CASE_BODY(raw_result__apply__expected_death__ok)
{
    const raw_result in_result(raw_result::expected_death, "Passthrough");
    const process::status status = process::status::fake_signaled(SIGINT, true);
    ATF_REQUIRE(in_result == in_result.apply(utils::make_optional(status)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__expected_exit__ok);
ATF_TEST_CASE_BODY(raw_result__apply__expected_exit__ok)
{
    const process::status success = process::status::fake_exited(EXIT_SUCCESS);
    const process::status failure = process::status::fake_exited(EXIT_FAILURE);

    const raw_result any_code(raw_result::expected_exit, none, "The reason");
    ATF_REQUIRE(any_code == any_code.apply(utils::make_optional(success)));
    ATF_REQUIRE(any_code == any_code.apply(utils::make_optional(failure)));

    const raw_result a_code(raw_result::expected_exit,
                            utils::make_optional(EXIT_FAILURE), "The reason");
    ATF_REQUIRE(a_code == a_code.apply(utils::make_optional(failure)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__expected_exit__broken);
ATF_TEST_CASE_BODY(raw_result__apply__expected_exit__broken)
{
    const process::status sig3 = process::status::fake_signaled(3, false);
    const process::status success = process::status::fake_exited(EXIT_SUCCESS);

    const raw_result any_code(raw_result::expected_exit, none, "The reason");
    ATF_REQUIRE(
        raw_result(raw_result::broken,
                   "Expected clean exit but received signal 3") ==
        any_code.apply(utils::make_optional(sig3)));

    const raw_result a_code(raw_result::expected_exit,
                            utils::make_optional(EXIT_FAILURE), "The reason");
    ATF_REQUIRE(
        raw_result(raw_result::broken,
                   "Expected clean exit with code 1 but got code 0") ==
        a_code.apply(utils::make_optional(success)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__expected_failure__ok);
ATF_TEST_CASE_BODY(raw_result__apply__expected_failure__ok)
{
    const process::status status = process::status::fake_exited(EXIT_SUCCESS);
    const raw_result xfailure(raw_result::expected_failure, "The reason");
    ATF_REQUIRE(xfailure == xfailure.apply(utils::make_optional(status)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__expected_failure__broken);
ATF_TEST_CASE_BODY(raw_result__apply__expected_failure__broken)
{
    const process::status success = process::status::fake_exited(EXIT_SUCCESS);
    const process::status failure = process::status::fake_exited(EXIT_FAILURE);
    const process::status sig3 = process::status::fake_signaled(3, true);

    const raw_result xfailure(raw_result::expected_failure, "The reason");
    ATF_REQUIRE(
        raw_result(raw_result::broken,
                   "Expected failure should have reported success but "
                   "exited with code 1") ==
        xfailure.apply(utils::make_optional(failure)));
    ATF_REQUIRE(
        raw_result(raw_result::broken,
                   "Expected failure should have reported success but "
                   "received signal 3") ==
        xfailure.apply(utils::make_optional(sig3)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__expected_signal__ok);
ATF_TEST_CASE_BODY(raw_result__apply__expected_signal__ok)
{
    const process::status sig1 = process::status::fake_signaled(1, false);
    const process::status sig3 = process::status::fake_signaled(3, true);

    const raw_result any_sig(raw_result::expected_signal, none, "The reason");
    ATF_REQUIRE(any_sig == any_sig.apply(utils::make_optional(sig1)));
    ATF_REQUIRE(any_sig == any_sig.apply(utils::make_optional(sig3)));

    const raw_result a_sig(raw_result::expected_signal,
                           utils::make_optional(3), "The reason");
    ATF_REQUIRE(a_sig == a_sig.apply(utils::make_optional(sig3)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__expected_signal__broken);
ATF_TEST_CASE_BODY(raw_result__apply__expected_signal__broken)
{
    const process::status sig5 = process::status::fake_signaled(5, false);
    const process::status success = process::status::fake_exited(EXIT_SUCCESS);

    const raw_result any_sig(raw_result::expected_signal, none, "The reason");
    ATF_REQUIRE(
        raw_result(raw_result::broken,
                   "Expected signal but exited with code 0") ==
        any_sig.apply(utils::make_optional(success)));

    const raw_result a_sig(raw_result::expected_signal,
                           utils::make_optional(4), "The reason");
    ATF_REQUIRE(
        raw_result(raw_result::broken, "Expected signal 4 but got 5") ==
        a_sig.apply(utils::make_optional(sig5)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__expected_timeout__ok);
ATF_TEST_CASE_BODY(raw_result__apply__expected_timeout__ok)
{
    const raw_result timeout(raw_result::expected_timeout, "The reason");
    ATF_REQUIRE(timeout == timeout.apply(none));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__expected_timeout__broken);
ATF_TEST_CASE_BODY(raw_result__apply__expected_timeout__broken)
{
    const process::status status = process::status::fake_exited(EXIT_SUCCESS);
    const raw_result timeout(raw_result::expected_timeout, "The reason");
    ATF_REQUIRE(
        raw_result(raw_result::broken,
                   "Expected timeout but exited with code 0") ==
        timeout.apply(utils::make_optional(status)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__failed__ok);
ATF_TEST_CASE_BODY(raw_result__apply__failed__ok)
{
    const process::status status = process::status::fake_exited(EXIT_FAILURE);
    const raw_result failed(raw_result::failed, "The reason");
    ATF_REQUIRE(failed == failed.apply(utils::make_optional(status)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__failed__broken);
ATF_TEST_CASE_BODY(raw_result__apply__failed__broken)
{
    const process::status success = process::status::fake_exited(EXIT_SUCCESS);
    const process::status failure = process::status::fake_exited(EXIT_FAILURE);
    const process::status sig3 = process::status::fake_signaled(3, true);

    const raw_result failed(raw_result::failed, "The reason");
    ATF_REQUIRE(
        raw_result(raw_result::broken,
                   "Failed test case should have reported failure but "
                   "exited with code 0") ==
        failed.apply(utils::make_optional(success)));
    ATF_REQUIRE(
        raw_result(raw_result::broken,
                   "Failed test case should have reported failure but "
                   "received signal 3") ==
        failed.apply(utils::make_optional(sig3)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__passed__ok);
ATF_TEST_CASE_BODY(raw_result__apply__passed__ok)
{
    const process::status status = process::status::fake_exited(EXIT_SUCCESS);
    const raw_result passed(raw_result::passed);
    ATF_REQUIRE(passed == passed.apply(utils::make_optional(status)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__passed__broken);
ATF_TEST_CASE_BODY(raw_result__apply__passed__broken)
{
    const process::status success = process::status::fake_exited(EXIT_SUCCESS);
    const process::status failure = process::status::fake_exited(EXIT_FAILURE);
    const process::status sig3 = process::status::fake_signaled(3, true);

    const raw_result passed(raw_result::passed);
    ATF_REQUIRE(
        raw_result(raw_result::broken,
                   "Passed test case should have reported success but "
                   "exited with code 1") ==
        passed.apply(utils::make_optional(failure)));
    ATF_REQUIRE(
        raw_result(raw_result::broken,
                   "Passed test case should have reported success but "
                   "received signal 3") ==
        passed.apply(utils::make_optional(sig3)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__skipped__ok);
ATF_TEST_CASE_BODY(raw_result__apply__skipped__ok)
{
    const process::status status = process::status::fake_exited(EXIT_SUCCESS);
    const raw_result skipped(raw_result::skipped, "The reason");
    ATF_REQUIRE(skipped == skipped.apply(utils::make_optional(status)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__apply__skipped__broken);
ATF_TEST_CASE_BODY(raw_result__apply__skipped__broken)
{
    const process::status success = process::status::fake_exited(EXIT_SUCCESS);
    const process::status failure = process::status::fake_exited(EXIT_FAILURE);
    const process::status sig3 = process::status::fake_signaled(3, true);

    const raw_result skipped(raw_result::skipped, "The reason");
    ATF_REQUIRE(
        raw_result(raw_result::broken,
                   "Skipped test case should have reported success but "
                   "exited with code 1") ==
        skipped.apply(utils::make_optional(failure)));
    ATF_REQUIRE(
        raw_result(raw_result::broken,
                   "Skipped test case should have reported success but "
                   "received signal 3") ==
        skipped.apply(utils::make_optional(sig3)));
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__externalize__broken);
ATF_TEST_CASE_BODY(raw_result__externalize__broken)
{
    const raw_result raw(raw_result::broken, "The reason");
    const engine::test_result expected(engine::test_result::broken,
                                       "The reason");
    ATF_REQUIRE(expected == raw.externalize());
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__externalize__expected_death);
ATF_TEST_CASE_BODY(raw_result__externalize__expected_death)
{
    const raw_result raw(raw_result::expected_death, "The reason");
    const engine::test_result expected(engine::test_result::expected_failure,
                                       "The reason");
    ATF_REQUIRE(expected == raw.externalize());
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__externalize__expected_exit);
ATF_TEST_CASE_BODY(raw_result__externalize__expected_exit)
{
    const raw_result raw(raw_result::expected_exit, "The reason");
    const engine::test_result expected(engine::test_result::expected_failure,
                                       "The reason");
    ATF_REQUIRE(expected == raw.externalize());
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__externalize__expected_failure);
ATF_TEST_CASE_BODY(raw_result__externalize__expected_failure)
{
    const raw_result raw(raw_result::expected_failure, "The reason");
    const engine::test_result expected(engine::test_result::expected_failure,
                                       "The reason");
    ATF_REQUIRE(expected == raw.externalize());
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__externalize__expected_signal);
ATF_TEST_CASE_BODY(raw_result__externalize__expected_signal)
{
    const raw_result raw(raw_result::expected_signal, "The reason");
    const engine::test_result expected(engine::test_result::expected_failure,
                                       "The reason");
    ATF_REQUIRE(expected == raw.externalize());
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__externalize__expected_timeout);
ATF_TEST_CASE_BODY(raw_result__externalize__expected_timeout)
{
    const raw_result raw(raw_result::expected_timeout, "The reason");
    const engine::test_result expected(engine::test_result::expected_failure,
                                       "The reason");
    ATF_REQUIRE(expected == raw.externalize());
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__externalize__failed);
ATF_TEST_CASE_BODY(raw_result__externalize__failed)
{
    const raw_result raw(raw_result::failed, "The reason");
    const engine::test_result expected(engine::test_result::failed,
                                       "The reason");
    ATF_REQUIRE(expected == raw.externalize());
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__externalize__passed);
ATF_TEST_CASE_BODY(raw_result__externalize__passed)
{
    const raw_result raw(raw_result::passed);
    const engine::test_result expected(engine::test_result::passed);
    ATF_REQUIRE(expected == raw.externalize());
}


ATF_TEST_CASE_WITHOUT_HEAD(raw_result__externalize__skipped);
ATF_TEST_CASE_BODY(raw_result__externalize__skipped)
{
    const raw_result raw(raw_result::skipped, "The reason");
    const engine::test_result expected(engine::test_result::skipped,
                                       "The reason");
    ATF_REQUIRE(expected == raw.externalize());
}


ATF_TEST_CASE_WITHOUT_HEAD(calculate_result__missing_file);
ATF_TEST_CASE_BODY(calculate_result__missing_file)
{
    using process::status;

    const status body_status = status::fake_exited(EXIT_SUCCESS);
    const status cleanup_status = status::fake_exited(EXIT_FAILURE);
    const engine::test_result expected(engine::test_result::broken,
                                       "Premature exit: exited with code 0");
    ATF_REQUIRE(expected == atf_iface::calculate_result(
        utils::make_optional(body_status), utils::make_optional(cleanup_status),
        fs::path("foo")));
}


ATF_TEST_CASE_WITHOUT_HEAD(calculate_result__bad_file);
ATF_TEST_CASE_BODY(calculate_result__bad_file)
{
    using process::status;

    const status body_status = status::fake_exited(EXIT_SUCCESS);
    atf::utils::create_file("foo", "invalid\n");
    const engine::test_result expected(engine::test_result::broken,
                                       "Unknown test result 'invalid'");
    ATF_REQUIRE(expected == atf_iface::calculate_result(
        utils::make_optional(body_status), none, fs::path("foo")));
}


ATF_TEST_CASE_WITHOUT_HEAD(calculate_result__body_ok__cleanup_ok);
ATF_TEST_CASE_BODY(calculate_result__body_ok__cleanup_ok)
{
    using process::status;

    atf::utils::create_file("result.txt", "skipped: Something\n");
    const status body_status = status::fake_exited(EXIT_SUCCESS);
    const status cleanup_status = status::fake_exited(EXIT_SUCCESS);
    ATF_REQUIRE(
        engine::test_result(engine::test_result::skipped, "Something") ==
        atf_iface::calculate_result(utils::make_optional(body_status),
                                    utils::make_optional(cleanup_status),
                                    fs::path("result.txt")));
}


ATF_TEST_CASE_WITHOUT_HEAD(calculate_result__body_ok__cleanup_bad);
ATF_TEST_CASE_BODY(calculate_result__body_ok__cleanup_bad)
{
    using process::status;

    atf::utils::create_file("result.txt", "skipped: Something\n");
    const status body_status = status::fake_exited(EXIT_SUCCESS);
    const status cleanup_status = status::fake_exited(EXIT_FAILURE);
    ATF_REQUIRE(
        engine::test_result(engine::test_result::broken, "Test case "
                            "cleanup did not terminate successfully") ==
        atf_iface::calculate_result(utils::make_optional(body_status),
                                    utils::make_optional(cleanup_status),
                                    fs::path("result.txt")));
}


ATF_TEST_CASE_WITHOUT_HEAD(calculate_result__body_ok__cleanup_timeout);
ATF_TEST_CASE_BODY(calculate_result__body_ok__cleanup_timeout)
{
    using process::status;

    atf::utils::create_file("result.txt", "skipped: Something\n");
    const status body_status = status::fake_exited(EXIT_SUCCESS);
    ATF_REQUIRE(
        engine::test_result(engine::test_result::broken, "Test case "
                            "cleanup timed out") ==
        atf_iface::calculate_result(utils::make_optional(body_status),
                                    none, fs::path("result.txt")));
}


ATF_TEST_CASE_WITHOUT_HEAD(calculate_result__body_bad__cleanup_ok);
ATF_TEST_CASE_BODY(calculate_result__body_bad__cleanup_ok)
{
    using process::status;

    atf::utils::create_file("result.txt", "skipped: Something\n");
    const status body_status = status::fake_exited(EXIT_FAILURE);
    const status cleanup_status = status::fake_exited(EXIT_SUCCESS);
    ATF_REQUIRE(
        engine::test_result(engine::test_result::broken, "Skipped test case "
                            "should have reported success but exited with "
                            "code 1") ==
        atf_iface::calculate_result(utils::make_optional(body_status),
                                    utils::make_optional(cleanup_status),
                                    fs::path("result.txt")));
}


ATF_TEST_CASE_WITHOUT_HEAD(calculate_result__body_bad__cleanup_bad);
ATF_TEST_CASE_BODY(calculate_result__body_bad__cleanup_bad)
{
    using process::status;

    atf::utils::create_file("result.txt", "passed\n");
    const status body_status = status::fake_signaled(3, false);
    const status cleanup_status = status::fake_exited(EXIT_FAILURE);
    ATF_REQUIRE(
        engine::test_result(engine::test_result::broken, "Passed test case "
                            "should have reported success but received "
                            "signal 3") ==
        atf_iface::calculate_result(utils::make_optional(body_status),
                                    utils::make_optional(cleanup_status),
                                    fs::path("result.txt")));
}


ATF_INIT_TEST_CASES(tcs)
{
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__empty);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__no_newline__unknown);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__no_newline__known);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__multiline__no_newline);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__multiline__with_newline);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__unknown_status__no_reason);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__unknown_status__with_reason);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__missing_reason__no_delim);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__missing_reason__bad_delim);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__missing_reason__empty);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__broken__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__broken__blanks);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__expected_death__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__expected_death__blanks);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__expected_exit__ok__any);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__expected_exit__ok__specific);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__expected_exit__bad_int);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__expected_failure__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__expected_failure__blanks);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__expected_signal__ok__any);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__expected_signal__ok__specific);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__expected_signal__bad_int);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__expected_timeout__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__expected_timeout__blanks);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__failed__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__failed__blanks);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__passed__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__passed__reason);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__skipped__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__parse__skipped__blanks);

    ATF_ADD_TEST_CASE(tcs, raw_result__load__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__load__missing_file);
    ATF_ADD_TEST_CASE(tcs, raw_result__load__format_error);

    ATF_ADD_TEST_CASE(tcs, raw_result__apply__broken__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__timed_out);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__expected_death__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__expected_exit__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__expected_exit__broken);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__expected_failure__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__expected_failure__broken);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__expected_signal__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__expected_signal__broken);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__expected_timeout__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__expected_timeout__broken);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__failed__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__failed__broken);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__passed__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__passed__broken);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__skipped__ok);
    ATF_ADD_TEST_CASE(tcs, raw_result__apply__skipped__broken);

    ATF_ADD_TEST_CASE(tcs, raw_result__externalize__broken);
    ATF_ADD_TEST_CASE(tcs, raw_result__externalize__expected_death);
    ATF_ADD_TEST_CASE(tcs, raw_result__externalize__expected_exit);
    ATF_ADD_TEST_CASE(tcs, raw_result__externalize__expected_failure);
    ATF_ADD_TEST_CASE(tcs, raw_result__externalize__expected_signal);
    ATF_ADD_TEST_CASE(tcs, raw_result__externalize__expected_timeout);
    ATF_ADD_TEST_CASE(tcs, raw_result__externalize__failed);
    ATF_ADD_TEST_CASE(tcs, raw_result__externalize__passed);
    ATF_ADD_TEST_CASE(tcs, raw_result__externalize__skipped);

    ATF_ADD_TEST_CASE(tcs, calculate_result__missing_file);
    ATF_ADD_TEST_CASE(tcs, calculate_result__bad_file);
    ATF_ADD_TEST_CASE(tcs, calculate_result__body_ok__cleanup_ok);
    ATF_ADD_TEST_CASE(tcs, calculate_result__body_ok__cleanup_bad);
    ATF_ADD_TEST_CASE(tcs, calculate_result__body_ok__cleanup_timeout);
    ATF_ADD_TEST_CASE(tcs, calculate_result__body_bad__cleanup_ok);
    ATF_ADD_TEST_CASE(tcs, calculate_result__body_bad__cleanup_bad);
}
