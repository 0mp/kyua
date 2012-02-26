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

extern "C" {
#include <sys/stat.h>

#include <unistd.h>
}

#include <fstream>

#include <atf-c++.hpp>
#include <lutok/exceptions.hpp>
#include <lutok/operations.hpp>
#include <lutok/wrap.hpp>

#include "engine/user_files/common.hpp"
#include "utils/env.hpp"
#include "utils/format/macros.hpp"
#include "utils/fs/path.hpp"
#include "utils/optional.ipp"

namespace fs = utils::fs;
namespace user_files = engine::user_files;

using utils::optional;


namespace {


/// Creates a mock module that can be called from syntax().
///
/// \pre mock_init() must have been called beforehand.
///
/// \param file The name of the file to create.
/// \param loaded_cookie A value that will be set in the global 'loaded_cookie'
///     variable within Lua to validate that nesting of module loading works
///     properly.
static void
create_mock_module(const char* file, const char* loaded_cookie)
{
    std::ofstream output((fs::path("luadir") / file).c_str());
    ATF_REQUIRE(output);
    output << F("return {export=function() _G.loaded_cookie = '%s' end}\n") %
        loaded_cookie;
}


/// Initializes mocking for Lua modules.
///
/// This creates a directory in which additional Lua modules will be placed and
/// puts a copy of the real 'init.lua' file in it.  It later updates KYUA_LUADIR
/// to point to this mock directory.
static void
mock_init(void)
{
    fs::path original_luadir(KYUA_LUADIR);
    const optional< std::string > env_luadir = utils::getenv("KYUA_LUADIR");
    if (env_luadir)
        original_luadir = fs::path(env_luadir.get());

    ATF_REQUIRE(::mkdir("luadir", 0755) != -1);
    utils::setenv("KYUA_LUADIR", "luadir");

    const fs::path init_lua = original_luadir / "init.lua";
    ATF_REQUIRE(::symlink(init_lua.c_str(), "luadir/init.lua") != -1);
}


}  // anonymous namespace


ATF_TEST_CASE_WITHOUT_HEAD(get_filename);
ATF_TEST_CASE_BODY(get_filename)
{
    mock_init();

    lutok::state state;
    user_files::init(state, fs::path("this/is/my-name"));

    lutok::eval(state, "init.get_filename()");
    ATF_REQUIRE_EQ("this/is/my-name", state.to_string());
    state.pop(1);
}


ATF_TEST_CASE_WITHOUT_HEAD(get_syntax__ok);
ATF_TEST_CASE_BODY(get_syntax__ok)
{
    mock_init();

    lutok::state state;
    user_files::init(state, fs::path("this/is/my-name"));

    create_mock_module("kyuafile_1.lua", "unused");
    lutok::do_string(state, "syntax('kyuafile', 1)");

    lutok::eval(state, "init.get_syntax().format");
    ATF_REQUIRE_EQ("kyuafile", state.to_string());
    lutok::eval(state, "init.get_syntax().version");
    ATF_REQUIRE_EQ(1, state.to_integer());
    state.pop(2);
}


ATF_TEST_CASE_WITHOUT_HEAD(get_syntax__fail);
ATF_TEST_CASE_BODY(get_syntax__fail)
{
    mock_init();

    lutok::state state;
    user_files::init(state, fs::path("the-name"));

    ATF_REQUIRE_THROW_RE(lutok::error, "Syntax not defined in file 'the-name'",
                         lutok::eval(state, "init.get_syntax()"));
}


ATF_TEST_CASE_WITHOUT_HEAD(run__simple);
ATF_TEST_CASE_BODY(run__simple)
{
    lutok::state state;
    user_files::init(state, fs::path("root.lua"));

    std::ofstream output("simple.lua");
    ATF_REQUIRE(output);
    output << "global_variable = 54321\n";
    output.close();

    lutok::do_string(state, "simple_env = init.run('simple.lua')");

    state.get_global("global_variable");
    ATF_REQUIRE(state.is_nil());
    state.pop(1);

    lutok::eval(state, "simple_env.global_variable");
    ATF_REQUIRE_EQ(54321, state.to_integer());
    state.pop(1);
}


ATF_TEST_CASE_WITHOUT_HEAD(run__chain);
ATF_TEST_CASE_BODY(run__chain)
{
    lutok::state state;
    user_files::init(state, fs::path("root.lua"));

    {
        std::ofstream output("simple1.lua");
        ATF_REQUIRE(output);
        output << "global_variable = 1\n";
        output << "env2 = init.run('simple2.lua')\n";
    }

    {
        std::ofstream output("simple2.lua");
        ATF_REQUIRE(output);
        output << "syntax('kyuafile', 1)\n";
        output << "global_variable = 2\n";
    }

    lutok::do_string(state, "env1 = init.run('simple1.lua')");

    lutok::do_string(state, "assert(global_variable == nil)");
    lutok::do_string(state, "assert(env1.global_variable == 1)");
    lutok::do_string(state, "assert(env1.env2.global_variable == 2)");

    ATF_REQUIRE_THROW(lutok::error,
                      lutok::do_string(state, "init.get_syntax()"));
    ATF_REQUIRE_THROW(lutok::error,
                      lutok::do_string(state, "init.env1.get_syntax()"));
    lutok::do_string(state,
                     "assert(env1.env2.init.get_syntax().format == "
                     "'kyuafile')");
    lutok::do_string(state,
                     "assert(env1.env2.init.get_syntax().version == 1)");
}


ATF_TEST_CASE_WITHOUT_HEAD(syntax__config_1__ok);
ATF_TEST_CASE_BODY(syntax__config_1__ok)
{
    mock_init();

    lutok::state state;
    user_files::init(state, fs::path("the-file"));

    create_mock_module("config_1.lua", "i-am-the-config");
    lutok::do_string(state, "syntax('config', 1)");

    lutok::eval(state, "init.get_syntax().format");
    ATF_REQUIRE_EQ("config", state.to_string());
    lutok::eval(state, "init.get_syntax().version");
    ATF_REQUIRE_EQ(1, state.to_integer());
    lutok::eval(state, "loaded_cookie");
    ATF_REQUIRE_EQ("i-am-the-config", state.to_string());
    state.pop(3);
}


ATF_TEST_CASE_WITHOUT_HEAD(syntax__config_1__version_error);
ATF_TEST_CASE_BODY(syntax__config_1__version_error)
{
    mock_init();

    lutok::state state;
    user_files::init(state, fs::path("the-file"));

    create_mock_module("config_1.lua", "unused");
    ATF_REQUIRE_THROW_RE(lutok::error, "Syntax request error: unknown version "
                         "2 for format 'config'",
                         lutok::do_string(state, "syntax('config', 2)"));

    ATF_REQUIRE_THROW_RE(lutok::error, "not defined",
                         lutok::eval(state, "init.get_syntax()"));

    lutok::eval(state, "loaded_cookie");
    ATF_REQUIRE(state.is_nil());
    state.pop(1);
}


ATF_TEST_CASE_WITHOUT_HEAD(syntax__config_1__missing_file);
ATF_TEST_CASE_BODY(syntax__config_1__missing_file)
{
    mock_init();

    lutok::state state;
    user_files::init(state, fs::path("the-file"));

    ATF_REQUIRE_THROW_RE(lutok::error, "config_1.lua",
                         lutok::do_string(state, "syntax('config', 1)"));

    ATF_REQUIRE_THROW_RE(lutok::error, "not defined",
                         lutok::eval(state, "init.get_syntax()"));

    lutok::eval(state, "loaded_cookie");
    ATF_REQUIRE(state.is_nil());
    state.pop(1);
}


ATF_TEST_CASE_WITHOUT_HEAD(syntax__kyuafile_1__ok);
ATF_TEST_CASE_BODY(syntax__kyuafile_1__ok)
{
    mock_init();

    lutok::state state;
    user_files::init(state, fs::path("the-file"));

    create_mock_module("kyuafile_1.lua", "i-am-the-kyuafile");
    lutok::do_string(state, "syntax('kyuafile', 1)");

    lutok::eval(state, "init.get_syntax().format");
    ATF_REQUIRE_EQ("kyuafile", state.to_string());
    lutok::eval(state, "init.get_syntax().version");
    ATF_REQUIRE_EQ(1, state.to_integer());
    lutok::eval(state, "loaded_cookie");
    ATF_REQUIRE_EQ("i-am-the-kyuafile", state.to_string());
    state.pop(3);
}


ATF_TEST_CASE_WITHOUT_HEAD(syntax__kyuafile_1__version_error);
ATF_TEST_CASE_BODY(syntax__kyuafile_1__version_error)
{
    mock_init();

    lutok::state state;
    user_files::init(state, fs::path("the-file"));

    create_mock_module("kyuafile_1.lua", "unused");
    ATF_REQUIRE_THROW_RE(lutok::error, "Syntax request error: unknown version 2 "
                         "for format 'kyuafile'",
                         lutok::do_string(state, "syntax('kyuafile', 2)"));

    ATF_REQUIRE_THROW_RE(lutok::error, "not defined",
                         lutok::eval(state, "init.get_syntax()"));

    lutok::eval(state, "loaded_cookie");
    ATF_REQUIRE(state.is_nil());
    state.pop(1);
}


ATF_TEST_CASE_WITHOUT_HEAD(syntax__kyuafile_1__missing_file);
ATF_TEST_CASE_BODY(syntax__kyuafile_1__missing_file)
{
    mock_init();

    lutok::state state;
    user_files::init(state, fs::path("the-file"));

    ATF_REQUIRE_THROW_RE(lutok::error, "kyuafile_1.lua",
                         lutok::do_string(state, "syntax('kyuafile', 1)"));

    ATF_REQUIRE_THROW_RE(lutok::error, "not defined",
                         lutok::eval(state, "init.get_syntax()"));

    lutok::eval(state, "loaded_cookie");
    ATF_REQUIRE(state.is_nil());
    state.pop(1);
}


ATF_TEST_CASE_WITHOUT_HEAD(syntax__format_error);
ATF_TEST_CASE_BODY(syntax__format_error)
{
    mock_init();

    lutok::state state;
    user_files::init(state, fs::path("the-file"));

    create_mock_module("kyuafile_1.lua", "unused");
    ATF_REQUIRE_THROW_RE(lutok::error, "Syntax request error: unknown format "
                         "'foo'",
                         lutok::do_string(state, "syntax('foo', 123)"));

    ATF_REQUIRE_THROW_RE(lutok::error, "not defined",
                         lutok::eval(state, "init.get_syntax()"));

    lutok::eval(state, "loaded_cookie");
    ATF_REQUIRE(state.is_nil());
    state.pop(1);
}


ATF_TEST_CASE_WITHOUT_HEAD(syntax__twice);
ATF_TEST_CASE_BODY(syntax__twice)
{
    mock_init();

    lutok::state state;
    user_files::init(state, fs::path("the-file"));

    create_mock_module("kyuafile_1.lua", "unused");
    ATF_REQUIRE_THROW_RE(lutok::error, "syntax.*more than once",
                         lutok::do_string(state, "syntax('kyuafile', 1); "
                                          "syntax('a', 3)"));
}


ATF_INIT_TEST_CASES(tcs)
{
    ATF_ADD_TEST_CASE(tcs, get_filename);

    ATF_ADD_TEST_CASE(tcs, get_syntax__ok);
    ATF_ADD_TEST_CASE(tcs, get_syntax__fail);

    ATF_ADD_TEST_CASE(tcs, run__simple);
    ATF_ADD_TEST_CASE(tcs, run__chain);

    ATF_ADD_TEST_CASE(tcs, syntax__config_1__ok);
    ATF_ADD_TEST_CASE(tcs, syntax__config_1__version_error);
    ATF_ADD_TEST_CASE(tcs, syntax__config_1__missing_file);
    ATF_ADD_TEST_CASE(tcs, syntax__kyuafile_1__ok);
    ATF_ADD_TEST_CASE(tcs, syntax__kyuafile_1__version_error);
    ATF_ADD_TEST_CASE(tcs, syntax__kyuafile_1__missing_file);
    ATF_ADD_TEST_CASE(tcs, syntax__format_error);
    ATF_ADD_TEST_CASE(tcs, syntax__twice);
}
