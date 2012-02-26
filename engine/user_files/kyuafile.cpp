// Copyright 2010, 2011 Google Inc.
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

#include <stdexcept>

#include <lutok/exceptions.hpp>
#include <lutok/operations.hpp>
#include <lutok/wrap.ipp>

#include "engine/atf_iface/test_program.hpp"
#include "engine/plain_iface/test_program.hpp"
#include "engine/user_files/common.hpp"
#include "engine/user_files/exceptions.hpp"
#include "engine/user_files/kyuafile.hpp"
#include "utils/format/macros.hpp"
#include "utils/fs/exceptions.hpp"
#include "utils/fs/operations.hpp"
#include "utils/optional.ipp"
#include "utils/sanity.hpp"

namespace atf_iface = engine::atf_iface;
namespace datetime = utils::datetime;
namespace fs = utils::fs;
namespace plain_iface = engine::plain_iface;
namespace user_files = engine::user_files;

using utils::none;
using utils::optional;


namespace {


/// Gets a string field from a Lua table.
///
/// \pre state(-1) contains a table.
///
/// \param state The Lua state.
/// \param field The name of the field to query.
/// \param error The error message to raise when an error condition is
///     encoutered.
///
/// \return The string value from the table.
///
/// \raises std::runtime_error If there is any problem accessing the table.
static inline std::string
get_table_string(lutok::state& state, const char* field,
                 const std::string& error)
{
    PRE(state.is_table());

    lutok::stack_cleaner cleaner(state);

    state.push_string(field);
    state.get_table();
    if (!state.is_string())
        throw std::runtime_error(error);
    const std::string str(state.to_string());
    state.pop(1);
    return str;
}


/// Gets a test program path name from a Lua test program definition.
///
/// \pre state(-1) contains a table representing a test program.
///
/// \param state The Lua state.
/// \param root The root location of the test suite.
///
/// \returns The path to the test program relative to root.
///
/// \raises std::runtime_error If the table definition is invalid or if the test
///     program does not exist.
static fs::path
get_path(lutok::state& state, const fs::path& root)
{
    const fs::path path = fs::path(get_table_string(
        state, "name", "Found non-string name for test program"));
    if (path.is_absolute())
        throw std::runtime_error(F("Got unexpected absolute path for test "
                                   "program '%s'") % path);

    if (!fs::exists(root / path))
        throw std::runtime_error(F("Non-existent test program '%s'") % path);

    return path;
}


/// Gets a test suite name from a Lua test program definition.
///
/// \pre state(-1) contains a table representing a test program.
///
/// \param state The Lua state.
/// \param path The path to the test program; used for error reporting purposes.
///
/// \returns The name of the test suite the test program belongs to.
///
/// \raises std::runtime_error If the table definition is invalid.
static std::string
get_test_suite(lutok::state& state, const fs::path& path)
{
    return get_table_string(
        state, "test_suite", F("Found non-string name for test suite of "
                               "test program '%s'") % path);
}


/// Gets the data of an ATF test program from the Lua state.
///
/// \pre stack(-1) contains a table describing a test program.
///
/// \param state The Lua state.
/// \param root The directory where the initial Kyuafile is located.
///
/// throw std::runtime_error If there is any problem in the input data.
/// throw fs::error If there is an invalid path in the input data.
static engine::test_program_ptr
get_atf_test_program(lutok::state& state, const fs::path& root)
{
    PRE(state.is_table());

    const fs::path path = get_path(state, root);
    const std::string test_suite = get_test_suite(state, path);

    return engine::test_program_ptr(new atf_iface::test_program(
        path, root, test_suite));
}


/// Gets the data of a plain test program from the Lua state.
///
/// \pre stack(-1) contains a table describing a test program.
///
/// \param state The Lua state.
/// \param root The directory where the initial Kyuafile is located.
///
/// throw std::runtime_error If there is any problem in the input data.
/// throw fs::error If there is an invalid path in the input data.
static engine::test_program_ptr
get_plain_test_program(lutok::state& state, const fs::path& root)
{
    PRE(state.is_table());

    lutok::stack_cleaner cleaner(state);

    const fs::path path = get_path(state, root);
    const std::string test_suite = get_test_suite(state, path);

    optional< datetime::delta > timeout;
    {
        state.push_string("timeout");
        state.get_table();
        if (state.is_nil())
            timeout = none;
        else if (state.is_number())
            timeout = datetime::delta(state.to_integer(), 0);
        else
            throw std::runtime_error(F("Non-integer value provided as timeout "
                                       "for test program '%s'") % path);
        state.pop(1);
    }

    return engine::test_program_ptr(new plain_iface::test_program(
        path, root, test_suite, timeout));
}


}  // anonymous namespace


// These namespace blocks are here to help Doxygen match the functions to their
// prototypes...
namespace engine {
namespace user_files {
namespace detail {


/// Gets the data of a test program from the Lua state.
///
/// \pre stack(-1) contains a table describing a test program.
///
/// \param state The Lua state.
/// \param root The directory where the initial Kyuafile is located.
///
/// throw std::runtime_error If there is any problem in the input data.
/// throw fs::error If there is an invalid path in the input data.
test_program_ptr
get_test_program(lutok::state& state, const fs::path& root)
{
    PRE(state.is_table());

    const std::string interface = get_table_string(
        state, "interface", "Missing test case interface");

    if (interface == "atf")
        return get_atf_test_program(state, root);
    else if (interface == "plain")
        return get_plain_test_program(state, root);
    else
        throw std::runtime_error(F("Unsupported test interface '%s'") %
                                 interface);
}


/// Gets the data of a collection of test programs from the Lua state.
///
/// \param state The Lua state.
/// \param expr The expression that evaluates to the table with the test program
///     data.
/// \param root The directory where the initial Kyuafile is located.
///
/// throw std::runtime_error If there is any problem in the input data.
/// throw fs::error If there is an invalid path in the input data.
test_programs_vector
get_test_programs(lutok::state& state, const std::string& expr,
                  const fs::path& root)
{
    lutok::stack_cleaner cleaner(state);

    lutok::eval(state, expr);
    if (!state.is_table())
        throw std::runtime_error(F("'%s' is not a table") % expr);

    test_programs_vector test_programs;

    state.push_nil();
    while (state.next()) {
        if (!state.is_table(-1))
            throw std::runtime_error(F("Expected table in '%s'") % expr);

        test_programs.push_back(get_test_program(state, root));

        state.pop(1);
    }

    return test_programs;
}


}  // namespace detail
}  // namespace user_files
}  // namespace engine


/// Constructs a kyuafile form initialized data.
///
/// Use load() to parse a test suite configuration file and construct a
/// kyuafile object.
///
/// \param root_ The root directory for the test suite represented by the
///     Kyuafile.  In other words, the directory containing the first Kyuafile
///     processed.
/// \param tps_ Collection of test programs that belong to this test suite.
user_files::kyuafile::kyuafile(const fs::path& root_,
                               const test_programs_vector& tps_) :
    _root(root_),
    _test_programs(tps_)
{
}


/// Parses a test suite configuration file.
///
/// \param file The file to parse.
///
/// \return High-level representation of the configuration file.
///
/// \throw load_error If there is any problem loading the file.  This includes
///     file access errors and syntax errors.
user_files::kyuafile
user_files::kyuafile::load(const utils::fs::path& file)
{
    test_programs_vector test_programs;
    try {
        lutok::state state;
        lutok::stack_cleaner cleaner(state);

        const user_files::syntax_def syntax = user_files::do_user_file(
            state, file);
        if (syntax.first != "kyuafile")
            throw std::runtime_error(F("Unexpected file format '%s'; "
                                       "need 'kyuafile'") % syntax.first);
        if (syntax.second != 1)
            throw std::runtime_error(F("Unexpected file version '%d'; "
                                       "only 1 is supported") % syntax.second);

        test_programs = detail::get_test_programs(state,
                                                  "kyuafile.TEST_PROGRAMS",
                                                  file.branch_path());
    } catch (const std::runtime_error& e) {
        throw load_error(file, e.what());
    }
    return kyuafile(file.branch_path(), test_programs);
}


/// Gets the root directory of the test suite.
///
/// \return A path.
const fs::path&
user_files::kyuafile::root(void) const
{
    return _root;
}


/// Gets the collection of test programs that belong to this test suite.
///
/// \return Collection of test program executable names.
const engine::test_programs_vector&
user_files::kyuafile::test_programs(void) const
{
    return _test_programs;
}
