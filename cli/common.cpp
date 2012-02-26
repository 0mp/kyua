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

#include "cli/common.hpp"
#include "engine/test_case.hpp"
#include "engine/user_files/config.hpp"
#include "engine/user_files/kyuafile.hpp"
#include "utils/cmdline/exceptions.hpp"
#include "utils/cmdline/parser.ipp"
#include "utils/format/macros.hpp"
#include "utils/fs/exceptions.hpp"
#include "utils/fs/operations.hpp"
#include "utils/fs/path.hpp"
#include "utils/env.hpp"
#include "utils/logging/macros.hpp"
#include "utils/optional.ipp"

namespace cmdline = utils::cmdline;
namespace fs = utils::fs;
namespace user_files = engine::user_files;

using utils::none;
using utils::optional;


namespace {


/// Path to the system-wide configuration files.
///
/// This is mutable so that tests can override it.  See set_confdir_for_testing.
static fs::path kyua_confdir(KYUA_CONFDIR);
#undef KYUA_CONFDIR


/// Basename of the user-specific configuration file.
static const char* user_config_basename = ".kyuarc";


/// Basename of the system-wide configuration file.
static const char* system_config_basename = "kyua.conf";


/// Textual description of the default configuration files.
///
/// This is just an auxiliary string required to define the option below, which
/// requires a pointer to a static C string.
static const std::string config_lookup_names =
    (fs::path("~") / user_config_basename).str() + " or " +
    (kyua_confdir / system_config_basename).str();


/// Gets the value of the HOME environment variable with path validation.
///
/// \return The value of the HOME environment variable if it is a valid path;
///     none if it is not defined or if it contains an invalid path.
static optional< fs::path >
get_home(void)
{
    const optional< std::string > home = utils::getenv("HOME");
    if (home) {
        try {
            return utils::make_optional(fs::path(home.get()));
        } catch (const fs::error& e) {
            LW(F("Invalid value '%s' in HOME environment variable: %s") %
               home.get() % e.what());
            return none;
        }
    } else
        return none;
}


/// Checks if a test program name matches a filter.
///
/// \param filter The filter to check against.
/// \param test_program The test program name to check against the filters.
///
/// \return Whether actual matches filter.
bool
match_test_program_only(const cli::test_filters::filter_pair& filter,
                        const fs::path& test_program)
{
    if (filter.first == test_program)
        return true;
    else
        return filter.second.empty() && filter.first.is_parent_of(test_program);
}


}  // anonymous namespace


/// Standard definition of the option to specify a configuration file.
///
/// You must use load_config() to load a configuration file while honoring the
/// value of this flag.
const cmdline::path_option cli::config_option(
    'c', "config",
    "Path to the configuration file",
    "file", config_lookup_names.c_str());


/// Standard definition of the option to specify a Kyuafile.
///
/// You must use load_kyuafile() to load a configuration file while honoring the
/// value of this flag.
const cmdline::path_option cli::kyuafile_option(
    'k', "kyuafile",
    "Path to the test suite definition",
    "file", "Kyuafile");


/// Loads the configuration file for this session, if any.
///
/// The algorithm implemented here is as follows:
/// 1) If ~/.kyuarc exists, load it and return.
/// 2) If sysconfdir/kyua.conf exists, load it and return.
/// 3) Otherwise, return the built-in settings.
///
/// \param cmdline The parsed command line.
///
/// \throw engine::error If the parsing of the configuration file fails.
///     TODO(jmmv): I'm not sure if this is the raised exception.  And even if
///     it is, we should make it more accurate.
user_files::config
cli::load_config(const cmdline::parsed_cmdline& cmdline)
{
    // TODO(jmmv): We should really be able to use cmdline.has_option here to
    // detect whether the option was provided or not instead of checking against
    // the default value.
    const fs::path filename = cmdline.get_option< cmdline::path_option >(
        config_option.long_name());
    if (filename.str() != config_option.default_value())
        return user_files::config::load(filename);

    const optional< fs::path > home = get_home();
    if (home) {
        const fs::path path = home.get() / user_config_basename;
        try {
            if (fs::exists(path))
                return user_files::config::load(path);
        } catch (const fs::error& e) {
            // Fall through.  If we fail to load the user-specific configuration
            // file because it cannot be openend, we try to load the system-wide
            // one.
            LW(F("Failed to load user-specific configuration file '%s': %s") %
               path % e.what());
        }
    }

    const fs::path path = kyua_confdir / system_config_basename;
    if (fs::exists(path))
        return user_files::config::load(path);
    else
        return user_files::config::defaults();
}


/// Loads the Kyuafile for this session or generates a fake one.
///
/// The algorithm implemented here is as follows:
/// 1) If there are arguments on the command line that are supposed to override
///    the Kyuafile, the Kyuafile is not loaded and a fake one is generated.
/// 2) Otherwise, the user-provided Kyuafile is loaded.
///
/// \param cmdline The parsed command line.
///
/// \throw engine::error If the parsing of the configuration file fails.
///     TODO(jmmv): I'm not sure if this is the raised exception.  And even if
///     it is, we should make it more accurate.
user_files::kyuafile
cli::load_kyuafile(const cmdline::parsed_cmdline& cmdline)
{
    const fs::path filename = cmdline.get_option< cmdline::path_option >(
        kyuafile_option.long_name());

    return user_files::kyuafile::load(filename);
}


/// Sets the value of the system-wide configuration directory.
///
/// Only use this for testing purposes.
///
/// \param dir The new value of the configuration directory.
void
cli::set_confdir_for_testing(const utils::fs::path& dir)
{
    kyua_confdir = dir;
}


/// Constructs a new set of filters.
///
/// \param user_filters The user-provided filters; if empty, no filters are
///     applied.  See parse_user_filters for details on the syntax.
///
/// \throw cmdline::usage_error If any of the filters is invalid.
cli::test_filters::test_filters(const std::vector< std::string >& user_filters)
{
    for (std::vector< std::string >::const_iterator iter = user_filters.begin();
         iter != user_filters.end(); iter++) {
        _filters.push_back(parse_user_filter(*iter));
    }
}


/// Parses a user-provided test filter.
///
/// \param str The user-provided string representing a filter for tests.  Must
///     be of the form &lt;test_program%gt;[:&lt;test_case%gt;].
///
/// \return The parsed filter, to be stored inside a test_filters object.
///
/// \throw cmdline::usage_error If the provided filter is invalid.
cli::test_filters::filter_pair
cli::test_filters::parse_user_filter(const std::string& str)
{
    if (str.empty())
        throw cmdline::usage_error("Test filter cannot be empty");

    const std::string::size_type pos = str.find(':');
    if (pos == 0)
        throw cmdline::usage_error(F("Program name component in '%s' is empty")
                                   % str);
    if (pos == str.length() - 1)
        throw cmdline::usage_error(F("Test case component in '%s' is empty")
                                   % str);

    try {
        const fs::path test_program(str.substr(0, pos));
        if (test_program.is_absolute())
            throw cmdline::usage_error(F("Program name '%s' must be relative "
                                         "to the test suite, not absolute") %
                                       test_program.str());
        const std::string test_case(pos == std::string::npos ?
                                    "" : str.substr(pos + 1));
        LD(F("Parsed user filter '%s': test program '%s', test case '%s'") %
           str % test_program.str() % test_case);
        return filter_pair(test_program, test_case);
    } catch (const fs::error& e) {
        throw cmdline::usage_error(F("Invalid path in filter '%s': %s") % str %
                                   e.what());
    }
}


/// Checks if a given test case identifier matches the set of filters.
///
/// \param id The identifier to check against the filters.
///
/// \return True if the provided identifier matches any filter.
bool
cli::test_filters::match_test_case(const engine::test_case_id& id) const
{
    if (_filters.empty()) {
        INV(match_test_program(id.program));
        return true;
    }

    bool matches = false;
    for (std::vector< filter_pair >::const_iterator iter = _filters.begin();
         !matches && iter != _filters.end(); iter++) {
        const filter_pair& filter = *iter;

        if (match_test_program_only(filter, id.program)) {
            if (filter.second.empty() || filter.second == id.name)
                matches = true;
        }
    }
    INV(!matches || match_test_program(id.program));
    return matches;
}


/// Checks if a given test program matches the set of filters.
///
/// This is provided as an optimization only, and the results of this function
/// are less specific than those of match_test_case.  Checking for the matching
/// of a test program should be done before loading the list of test cases from
/// a program, so as to avoid the delay in executing the test program, but
/// match_test_case must still be called afterwards.
///
/// \param name The test program to check against the filters.
///
/// \return True if the provided identifier matches any filter.
bool
cli::test_filters::match_test_program(const fs::path& name) const
{
    if (_filters.empty())
        return true;

    bool matches = false;
    for (std::vector< filter_pair >::const_iterator iter = _filters.begin();
         !matches && iter != _filters.end(); iter++) {
        if (match_test_program_only(*iter, name))
            matches = true;
    }
    return matches;
}
