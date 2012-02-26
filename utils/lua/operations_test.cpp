// Copyright 2011, Google Inc.
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

#include <fstream>

#include <atf-c++.hpp>

#include "utils/format/macros.hpp"
#include "utils/lua/exceptions.hpp"
#include "utils/lua/operations.hpp"
#include "utils/lua/test_utils.hpp"
#include "utils/lua/wrap.hpp"

namespace fs = utils::fs;
namespace lua = utils::lua;


ATF_TEST_CASE_WITHOUT_HEAD(do_file__any_results);
ATF_TEST_CASE_BODY(do_file__any_results)
{
    std::ofstream output("test.lua");
    output << "return 10, 20, 30\n";
    output.close();

    lua::state state;
    ATF_REQUIRE_EQ(3, lua::do_file(state, fs::path("test.lua"), -1));
    ATF_REQUIRE_EQ(3, state.get_top());
    ATF_REQUIRE_EQ(10, state.to_integer(-3));
    ATF_REQUIRE_EQ(20, state.to_integer(-2));
    ATF_REQUIRE_EQ(30, state.to_integer(-1));
    state.pop(3);
}


ATF_TEST_CASE_WITHOUT_HEAD(do_file__no_results);
ATF_TEST_CASE_BODY(do_file__no_results)
{
    std::ofstream output("test.lua");
    output << "return 10, 20, 30\n";
    output.close();

    lua::state state;
    ATF_REQUIRE_EQ(0, lua::do_file(state, fs::path("test.lua")));
    ATF_REQUIRE_EQ(0, state.get_top());
}


ATF_TEST_CASE_WITHOUT_HEAD(do_file__many_results);
ATF_TEST_CASE_BODY(do_file__many_results)
{
    std::ofstream output("test.lua");
    output << "return 10, 20, 30\n";
    output.close();

    lua::state state;
    ATF_REQUIRE_EQ(2, lua::do_file(state, fs::path("test.lua"), 2));
    ATF_REQUIRE_EQ(2, state.get_top());
    ATF_REQUIRE_EQ(10, state.to_integer(-2));
    ATF_REQUIRE_EQ(20, state.to_integer(-1));
    state.pop(2);
}


ATF_TEST_CASE_WITHOUT_HEAD(do_file__not_found);
ATF_TEST_CASE_BODY(do_file__not_found)
{
    lua::state state;
    stack_balance_checker checker(state);
    ATF_REQUIRE_THROW_RE(lua::error, "Failed to load Lua file 'foobar.lua'",
                         lua::do_file(state, fs::path("foobar.lua")));
}


ATF_TEST_CASE_WITHOUT_HEAD(do_file__error);
ATF_TEST_CASE_BODY(do_file__error)
{
    std::ofstream output("test.lua");
    output << "a b c\n";
    output.close();

    lua::state state;
    stack_balance_checker checker(state);
    ATF_REQUIRE_THROW_RE(lua::error, "Failed to load Lua file 'test.lua'",
                         lua::do_file(state, fs::path("test.lua")));
}


ATF_TEST_CASE_WITHOUT_HEAD(do_string__any_results);
ATF_TEST_CASE_BODY(do_string__any_results)
{
    lua::state state;
    ATF_REQUIRE_EQ(3, lua::do_string(state, "return 10, 20, 30", -1));
    ATF_REQUIRE_EQ(3, state.get_top());
    ATF_REQUIRE_EQ(10, state.to_integer(-3));
    ATF_REQUIRE_EQ(20, state.to_integer(-2));
    ATF_REQUIRE_EQ(30, state.to_integer(-1));
    state.pop(3);
}


ATF_TEST_CASE_WITHOUT_HEAD(do_string__no_results);
ATF_TEST_CASE_BODY(do_string__no_results)
{
    lua::state state;
    ATF_REQUIRE_EQ(0, lua::do_string(state, "return 10, 20, 30"));
    ATF_REQUIRE_EQ(0, state.get_top());
}


ATF_TEST_CASE_WITHOUT_HEAD(do_string__many_results);
ATF_TEST_CASE_BODY(do_string__many_results)
{
    lua::state state;
    ATF_REQUIRE_EQ(2, lua::do_string(state, "return 10, 20, 30", 2));
    ATF_REQUIRE_EQ(2, state.get_top());
    ATF_REQUIRE_EQ(10, state.to_integer(-2));
    ATF_REQUIRE_EQ(20, state.to_integer(-1));
    state.pop(2);
}


ATF_TEST_CASE_WITHOUT_HEAD(do_string__error);
ATF_TEST_CASE_BODY(do_string__error)
{
    lua::state state;
    stack_balance_checker checker(state);
    ATF_REQUIRE_THROW_RE(lua::error, "Failed to process Lua string 'a b c'",
                         lua::do_string(state, "a b c"));
}


ATF_TEST_CASE_WITHOUT_HEAD(get_array_as_strings__empty);
ATF_TEST_CASE_BODY(get_array_as_strings__empty)
{
    lua::state state;
    lua::do_string(state, "the_array = {}");
    stack_balance_checker checker(state);
    const std::vector< std::string > array = lua::get_array_as_strings(
        state, "the_array");
    ATF_REQUIRE(array.empty());
}


ATF_TEST_CASE_WITHOUT_HEAD(get_array_as_strings__some);
ATF_TEST_CASE_BODY(get_array_as_strings__some)
{
    lua::state state;
    lua::do_string(state, "module = {};"
                   "local aux = \"abcd\";"
                   "module.the_array = {\"efg\", aux, 5};");
    stack_balance_checker checker(state);
    const std::vector< std::string > array = lua::get_array_as_strings(
        state, "module.the_array");
    ATF_REQUIRE_EQ(3, array.size());
    ATF_REQUIRE_EQ("efg", array[0]);
    ATF_REQUIRE_EQ("abcd", array[1]);
    ATF_REQUIRE_EQ("5", array[2]);
}


ATF_TEST_CASE_WITHOUT_HEAD(get_array_as_strings__nil);
ATF_TEST_CASE_BODY(get_array_as_strings__nil)
{
    lua::state state;
    stack_balance_checker checker(state);
    ATF_REQUIRE_THROW_RE(lua::error, "Undefined array 'abc'",
                         lua::get_array_as_strings(state, "abc"));
}


ATF_TEST_CASE_WITHOUT_HEAD(get_array_as_strings__not_a_table);
ATF_TEST_CASE_BODY(get_array_as_strings__not_a_table)
{
    lua::state state;
    lua::do_string(state, "fake = \"not a table!\"");
    stack_balance_checker checker(state);
    ATF_REQUIRE_THROW_RE(lua::error, "'fake' not an array",
                         lua::get_array_as_strings(state, "fake"));
}


ATF_TEST_CASE_WITHOUT_HEAD(get_array_as_strings__not_a_string);
ATF_TEST_CASE_BODY(get_array_as_strings__not_a_string)
{
    lua::state state;
    lua::do_string(state, "function foo() return 3; end;"
                   "bad = {\"abc\", foo};");
    stack_balance_checker checker(state);
    ATF_REQUIRE_THROW_RE(lua::error, "non-string value",
                         lua::get_array_as_strings(state, "bad"));
}


ATF_INIT_TEST_CASES(tcs)
{
    ATF_ADD_TEST_CASE(tcs, do_file__any_results);
    ATF_ADD_TEST_CASE(tcs, do_file__no_results);
    ATF_ADD_TEST_CASE(tcs, do_file__many_results);
    ATF_ADD_TEST_CASE(tcs, do_file__not_found);
    ATF_ADD_TEST_CASE(tcs, do_file__error);

    ATF_ADD_TEST_CASE(tcs, do_string__any_results);
    ATF_ADD_TEST_CASE(tcs, do_string__no_results);
    ATF_ADD_TEST_CASE(tcs, do_string__many_results);
    ATF_ADD_TEST_CASE(tcs, do_string__error);

    ATF_ADD_TEST_CASE(tcs, get_array_as_strings__empty);
    ATF_ADD_TEST_CASE(tcs, get_array_as_strings__some);
    ATF_ADD_TEST_CASE(tcs, get_array_as_strings__nil);
    ATF_ADD_TEST_CASE(tcs, get_array_as_strings__not_a_table);
    ATF_ADD_TEST_CASE(tcs, get_array_as_strings__not_a_string);
}
