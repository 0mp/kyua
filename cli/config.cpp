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

#include "cli/config.hpp"
#include "engine/user_files/config.hpp"
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


/// Basename of the user-specific configuration file.
static const char* user_config_basename = ".kyuarc";


/// Basename of the system-wide configuration file.
static const char* system_config_basename = "kyua.conf";


/// Magic string to disable loading of configuration files.
static const char* none_config = "none";


/// Textual description of the default configuration files.
///
/// This is just an auxiliary string required to define the option below, which
/// requires a pointer to a static C string.
///
/// \todo If the user overrides the KYUA_CONFDIR environment variable, we don't
/// reflect this fact here.  We don't want to query the variable during program
/// initialization due to the side-effects it may have.  Therefore, fixing this
/// is tricky as it may require a whole rethink of this module.
static const std::string config_lookup_names =
    (fs::path("~") / user_config_basename).str() + " or " +
    (fs::path(KYUA_CONFDIR) / system_config_basename).str();


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


/// Loads the configuration file for this session, if any.
///
/// This is a helper function that does not apply user-specified overrides.  See
/// the documentation for cli::load_config() for more details.
///
/// \param cmdline The parsed command line.
///
/// \throw engine::error If the parsing of the configuration file fails.
///     TODO(jmmv): I'm not sure if this is the raised exception.  And even if
///     it is, we should make it more accurate.
user_files::config
load_config_file(const cmdline::parsed_cmdline& cmdline)
{
    // TODO(jmmv): We should really be able to use cmdline.has_option here to
    // detect whether the option was provided or not instead of checking against
    // the default value.
    const fs::path filename = cmdline.get_option< cmdline::path_option >(
        cli::config_option.long_name());
    if (filename.str() == none_config) {
        LD("Configuration loading disabled; using defaults");
        return user_files::config::defaults();
    } else if (filename.str() != cli::config_option.default_value())
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

    const fs::path confdir(utils::getenv_with_default(
        "KYUA_CONFDIR", KYUA_CONFDIR));

    const fs::path path = confdir / system_config_basename;
    if (fs::exists(path)) {
        return user_files::config::load(path);
    } else {
        return user_files::config::defaults();
    }
}


}  // anonymous namespace


/// Standard definition of the option to specify a configuration file.
///
/// You must use load_config() to load a configuration file while honoring the
/// value of this flag.
const cmdline::path_option cli::config_option(
    'c', "config",
    (std::string("Path to the configuration file; '") + none_config +
     "' to disable loading").c_str(),
    "file", config_lookup_names.c_str());


/// Standard definition of the option to specify a configuration variable.
///
/// You must use load_kyuafile() to load a configuration file while honoring the
/// value of this flag.
const cmdline::property_option cli::variable_option(
    'v', "variable",
    "Overrides a particular configuration variable",
    "name=value");


/// Loads the configuration file for this session, if any.
///
/// The algorithm implemented here is as follows:
/// 1) If ~/.kyuarc exists, load it.
/// 2) Otherwise, if sysconfdir/kyua.conf exists, load it.
/// 3) Otherwise, use the built-in settings.
/// 4) Lastly, apply any user-provided overrides.
///
/// \param cmdline The parsed command line.
///
/// \throw engine::error If the parsing of the configuration file fails.
///     TODO(jmmv): I'm not sure if this is the raised exception.  And even if
///     it is, we should make it more accurate.
user_files::config
cli::load_config(const cmdline::parsed_cmdline& cmdline)
{
    const user_files::config config = load_config_file(cmdline);

    if (cmdline.has_option(variable_option.long_name())) {
        return config.apply_overrides(
            cmdline.get_multi_option< cmdline::property_option >(
                variable_option.long_name()));
    } else {
        return config;
    }
}
