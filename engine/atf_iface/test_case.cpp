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

#include "engine/atf_iface/test_case.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <sstream>

#include "engine/atf_iface/runner.hpp"
#include "engine/exceptions.hpp"
#include "engine/metadata.hpp"
#include "engine/test_program.hpp"
#include "engine/test_result.hpp"
#include "utils/config/tree.ipp"
#include "utils/fs/exceptions.hpp"
#include "utils/fs/operations.hpp"
#include "utils/format/macros.hpp"
#include "utils/memory.hpp"
#include "utils/optional.ipp"
#include "utils/passwd.hpp"
#include "utils/sanity.hpp"
#include "utils/units.hpp"

namespace atf_iface = engine::atf_iface;
namespace config = utils::config;
namespace datetime = utils::datetime;
namespace fs = utils::fs;
namespace passwd = utils::passwd;
namespace units = utils::units;

using utils::none;
using utils::optional;


namespace {


/// The default timeout value for test cases that do not provide one.
/// TODO(jmmv): We should not be doing this; see issue 5 for details.
static datetime::delta default_timeout(300, 0);


/// Concatenates a collection of objects in a string using ' ' as a separator.
///
/// \param set The objects to join.  This cannot be empty.
///
/// \return The concatenation of all the objects in the set.
template< class T >
std::string
flatten_set(const std::set< T >& set)
{
    PRE(!set.empty());

    std::ostringstream output;
    std::copy(set.begin(), set.end(), std::ostream_iterator< T >(output, " "));

    std::string result = output.str();
    result.erase(result.end() - 1);
    return result;
}


/// Executes the test case.
///
/// This should not throw any exception: problems detected during execution are
/// reported as a broken test case result.
///
/// \param test_case The test case to debug or run.
/// \param user_config The run-time configuration for the test case.
/// \param hooks Hooks to introspect the execution of the test case.
/// \param stdout_path The file to which to redirect the stdout of the test.
///     If none, use a temporary file in the work directory.
/// \param stderr_path The file to which to redirect the stdout of the test.
///     If none, use a temporary file in the work directory.
///
/// \return The result of the execution.
static engine::test_result
execute(const engine::base_test_case* test_case,
        const config::tree& user_config,
        engine::test_case_hooks& hooks,
        const optional< fs::path >& stdout_path,
        const optional< fs::path >& stderr_path)
{
    const engine::atf_iface::test_case* tc =
        dynamic_cast< const engine::atf_iface::test_case* >(test_case);
    if (tc->fake_result())
        return tc->fake_result().get();
    else
        return engine::atf_iface::run_test_case(
            *tc, user_config, hooks, stdout_path, stderr_path);
}


}  // anonymous namespace


/// Parses a boolean property.
///
/// \param name The name of the property; used for error messages.
/// \param value The textual value to process.
///
/// \return The value as a boolean.
///
/// \throw engine::format_error If the value is invalid.
bool
engine::atf_iface::detail::parse_bool(const std::string& name,
                                      const std::string& value)
{
    if (value == "true" || value == "yes")
        return true;
    else if (value == "false" || value == "no")
        return false;
    else
        throw format_error(F("Invalid value '%s' for boolean property '%s'") %
                           value % name);
}


/// Parses an integer property.
///
/// \param name The name of the property; used for error messages.
/// \param value The textual value to process.
///
/// \return The value as an integer.
///
/// \throw engine::format_error If the value is invalid.
unsigned long
engine::atf_iface::detail::parse_ulong(const std::string& name,
                                       const std::string& value)
{
    if (value.empty())
        throw format_error(F("Invalid empty value for integer property '%s'") %
                           name);

    char* endptr;
    const unsigned long l = std::strtoul(value.c_str(), &endptr, 10);
    if (value.find_first_of("- \t") != std::string::npos || *endptr != '\0' ||
        (l == 0 && errno == EINVAL) ||
        (l == std::numeric_limits< unsigned long >::max() && errno == ERANGE))
        throw format_error(F("Invalid value '%s' for integer property '%s'") %
                           value % name);
    return l;
}


/// Internal implementation of a test case.
struct engine::atf_iface::test_case::impl {
    /// The test case description.
    std::string description;

    /// Whether the test case has a cleanup routine or not.
    bool has_cleanup;

    /// The maximum amount of time the test case can run for.
    datetime::delta timeout;

    /// Test case metadata.
    metadata md;

    /// User-defined meta-data properties.
    properties_map user_metadata;

    /// Fake result to return instead of running the test case.
    optional< test_result > fake_result;

    /// Constructor.
    ///
    /// \param description_ See the parent class.
    /// \param has_cleanup_ See the parent class.
    /// \param timeout_ See the parent class.
    /// \param md_ See the parent class.
    /// \param user_metadata_ See the parent class.
    /// \param fake_result_ Fake result to return instead of running the test
    ///     case.
    impl(const std::string& description_,
         const bool has_cleanup_,
         const datetime::delta& timeout_,
         const metadata& md_,
         const properties_map& user_metadata_,
         const optional< test_result >& fake_result_) :
        description(description_),
        has_cleanup(has_cleanup_),
        timeout(timeout_),
        md(md_),
        user_metadata(user_metadata_),
        fake_result(fake_result_)
    {
        for (properties_map::const_iterator iter = user_metadata.begin();
             iter != user_metadata.end(); iter++) {
            const std::string& property_name = (*iter).first;
            PRE_MSG(property_name.size() > 2 &&
                    property_name.substr(0, 2) == "X-",
                    "User properties must be prefixed by X-");
        }
    }
};


/// Constructs a new test case.
///
/// \param test_program_ The test program this test case belongs to.  This
///     object must exist during the lifetime of the test case.
/// \param name_ The name of the test case.
/// \param description_ The description of the test case, if any.
/// \param has_cleanup_ Whether the test case has a cleanup routine or not.
/// \param timeout_ The maximum time the test case can run for.
/// \param md_ The test case metadata.
/// \param user_metadata_ User-defined meta-data properties.  The names of all
///     of these properties must start by 'X-'.
atf_iface::test_case::test_case(const base_test_program& test_program_,
                                const std::string& name_,
                                const std::string& description_,
                                const bool has_cleanup_,
                                const datetime::delta& timeout_,
                                const metadata& md_,
                                const properties_map& user_metadata_) :
    base_test_case("atf", test_program_, name_),
    _pimpl(new impl(description_, has_cleanup_, timeout_, md_,
                    user_metadata_, none))
{
}


/// Constructs a new fake test case.
///
/// A fake test case is a test case that is not really defined by the test
/// program.  Such test cases have a name surrounded by '__' and, when executed,
/// they return a fixed, pre-recorded result.  This functionality is used, for
/// example, to dynamically create a test case representing the test program
/// itself when it is broken (i.e. when it's even unable to provide a list of
/// its own test cases).
///
/// \param test_program_ The test program this test case belongs to.
/// \param name_ The name to give to this fake test case.  This name has to be
///     prefixed and suffixed by '__' to clearly denote that this is internal.
/// \param description_ The description of the test case, if any.
/// \param test_result_ The fake result to return when this test case is run.
atf_iface::test_case::test_case(const base_test_program& test_program_,
                                const std::string& name_,
                                const std::string& description_,
                                const engine::test_result& test_result_) :
    base_test_case("atf", test_program_, name_),
    _pimpl(new impl(description_, false, default_timeout,
                    metadata_builder().build(), properties_map(),
                    utils::make_optional(test_result_)))
{
    PRE_MSG(name_.length() > 4 && name_.substr(0, 2) == "__" &&
            name_.substr(name_.length() - 2) == "__",
            "Invalid fake name provided to fake test case");
}


/// Destructor.
atf_iface::test_case::~test_case(void)
{
}


/// Creates a test case from a set of raw properties (the test program output).
///
/// \param test_program_ The test program this test case belongs to.  This
///     object must exist during the lifetime of the test case.
/// \param name_ The name of the test case.
/// \param raw_properties The properties (name/value string pairs) as provided
///     by the test program.
///
/// \return A new test_case.
///
/// \throw engine::format_error If the syntax of any of the properties is
///     invalid.
atf_iface::test_case
atf_iface::test_case::from_properties(const base_test_program& test_program_,
                                      const std::string& name_,
                                      const properties_map& raw_properties)
{
    std::string description_;
    bool has_cleanup_ = false;
    datetime::delta timeout_ = default_timeout;
    metadata_builder mdbuilder;
    properties_map user_metadata_;

    try {
        for (properties_map::const_iterator iter = raw_properties.begin();
             iter != raw_properties.end(); iter++) {
            const std::string& name = (*iter).first;
            const std::string& value = (*iter).second;

            if (name == "descr") {
                description_ = value;
            } else if (name == "has.cleanup") {
                has_cleanup_ = detail::parse_bool(name, value);
            } else if (name == "require.arch") {
                mdbuilder.set_string("allowed_architectures", value);
            } else if (name == "require.config") {
                mdbuilder.set_string("required_configs", value);
            } else if (name == "require.files") {
                mdbuilder.set_string("required_files", value);
            } else if (name == "require.machine") {
                mdbuilder.set_string("allowed_platforms", value);
            } else if (name == "require.memory") {
                mdbuilder.set_string("required_memory", value);
            } else if (name == "require.progs") {
                mdbuilder.set_string("required_programs", value);
            } else if (name == "require.user") {
                mdbuilder.set_string("required_user", value);
            } else if (name == "timeout") {
                timeout_ = datetime::delta(detail::parse_ulong(name, value), 0);
            } else if (name.length() > 2 && name.substr(0, 2) == "X-") {
                user_metadata_[name] = value;
            } else {
                throw engine::format_error(F("Unknown test case metadata "
                                             "property '%s'") % name);
            }
        }
    } catch (const config::error& e) {
        throw engine::format_error(e.what());
    }

    return test_case(test_program_, name_, description_, has_cleanup_,
                     timeout_, mdbuilder.build(), user_metadata_);
}


/// Gets the description of the test case.
///
/// \return The description of the test case.
const std::string&
atf_iface::test_case::description(void) const
{
    return _pimpl->description;
}


/// Gets whether the test case has a cleanup routine or not.
///
/// \return True if the test case has a cleanup routine, false otherwise.
bool
atf_iface::test_case::has_cleanup(void) const
{
    return _pimpl->has_cleanup;
}


/// Gets the test case timeout.
///
/// \return The test case timeout.
const datetime::delta&
atf_iface::test_case::timeout(void) const
{
    return _pimpl->timeout;
}


/// Gets the test case metadata.
///
/// \return The test case metadata.
const engine::metadata&
atf_iface::test_case::get_metadata(void) const
{
    return _pimpl->md;
}


/// Gets the list of allowed architectures.
///
/// \return The list of allowed architectures.
const engine::strings_set&
atf_iface::test_case::allowed_architectures(void) const
{
    return _pimpl->md.allowed_architectures();
}


/// Gets the list of allowed platforms.
///
/// \return The list of allowed platforms.
const engine::strings_set&
atf_iface::test_case::allowed_platforms(void) const
{
    return _pimpl->md.allowed_platforms();
}


/// Gets the list of required configuration variables.
///
/// \return The list of required configuration variables.
const engine::strings_set&
atf_iface::test_case::required_configs(void) const
{
    return _pimpl->md.required_configs();
}


/// Gets the list of required files.
///
/// \return The list of required files.
const engine::paths_set&
atf_iface::test_case::required_files(void) const
{
    return _pimpl->md.required_files();
}


/// Gets the required memory.
///
/// \return The required memory.
const units::bytes&
atf_iface::test_case::required_memory(void) const
{
    return _pimpl->md.required_memory();
}


/// Gets the list of required programs.
///
/// \return The list of required programs.
const engine::paths_set&
atf_iface::test_case::required_programs(void) const
{
    return _pimpl->md.required_programs();
}


/// Gets the required user name.
///
/// \return The required user name.
const std::string&
atf_iface::test_case::required_user(void) const
{
    return _pimpl->md.required_user();
}


/// Gets the custom user metadata, if any.
///
/// \return The user metadata.
const engine::properties_map&
atf_iface::test_case::user_metadata(void) const
{
    return _pimpl->user_metadata;
}


/// Gets the fake result pre-stored for this test case.
///
/// \return A fake result, or none if not defined.
optional< engine::test_result >
atf_iface::test_case::fake_result(void) const
{
    return _pimpl->fake_result;
}


/// Returns a string representation of all test case properties.
///
/// The returned keys and values match those that can be defined by the test
/// case.
///
/// \return A key/value mapping describing all the test case properties.
engine::properties_map
atf_iface::test_case::get_all_properties(void) const
{
    properties_map props = _pimpl->user_metadata;

    // TODO(jmmv): This is unnecessary.  We just need to let the caller query
    // the metadata object and convert that to a properties map directly.
    if (!_pimpl->description.empty())
        props["descr"] = _pimpl->description;
    if (_pimpl->has_cleanup)
        props["has.cleanup"] = "true";
    if (_pimpl->timeout != default_timeout) {
        INV(_pimpl->timeout.useconds == 0);
        props["timeout"] = F("%s") % _pimpl->timeout.seconds;
    }
    if (!allowed_architectures().empty())
        props["require.arch"] = flatten_set(allowed_architectures());
    if (!allowed_platforms().empty())
        props["require.machine"] = flatten_set(allowed_platforms());
    if (!required_configs().empty())
        props["require.config"] = flatten_set(required_configs());
    if (!required_files().empty())
        props["require.files"] = flatten_set(required_files());
    if (required_memory() > 0)
        props["require.memory"] = required_memory().format();
    if (!required_programs().empty())
        props["require.progs"] = flatten_set(required_programs());
    if (!required_user().empty())
        props["require.user"] = required_user();

    return props;
}


/// Checks if all the requirements specified by the test case are met.
///
/// \param user_config The engine configuration.
///
/// \return A string describing what is missing; empty if everything is OK.
std::string
atf_iface::test_case::check_requirements(const config::tree& user_config) const
{
    const strings_set& required_configs_ = required_configs();
    for (strings_set::const_iterator iter = required_configs_.begin();
         iter != required_configs_.end(); iter++) {
        std::string property;
        if ((*iter) == "unprivileged-user" || (*iter) == "unprivileged_user")
            property = "unprivileged_user";
        else
            property = F("test_suites.%s.%s") %
                test_program().test_suite_name() % (*iter);

        if (!user_config.is_set(property))
            return F("Required configuration property '%s' not defined") %
                (*iter);
    }

    const strings_set& allowed_architectures_ = allowed_architectures();
    if (!allowed_architectures_.empty()) {
        const std::string architecture =
            user_config.lookup< config::string_node >("architecture");
        if (allowed_architectures_.find(architecture) ==
            allowed_architectures_.end())
            return F("Current architecture '%s' not supported") % architecture;
    }

    const strings_set& allowed_platforms_ = allowed_platforms();
    if (!allowed_platforms_.empty()) {
        const std::string platform =
            user_config.lookup< config::string_node >("platform");
        if (allowed_platforms_.find(platform) == allowed_platforms_.end())
            return F("Current platform '%s' not supported") % platform;
    }

    const std::string& required_user_ = required_user();
    if (!required_user_.empty()) {
        const passwd::user user = passwd::current_user();
        if (required_user_ == "root") {
            if (!user.is_root())
                return "Requires root privileges";
        } else if (required_user_ == "unprivileged") {
            if (user.is_root())
                if (!user_config.is_set("unprivileged_user"))
                    return "Requires an unprivileged user but the "
                        "unprivileged-user configuration variable is not "
                        "defined";
        } else
            UNREACHABLE_MSG("Value of require.user not properly validated");
    }

    const paths_set& required_files_ = required_files();
    for (paths_set::const_iterator iter = required_files_.begin();
         iter != required_files_.end(); iter++) {
        INV((*iter).is_absolute());
        if (!fs::exists(*iter))
            return F("Required file '%s' not found") % *iter;
    }

    const paths_set& required_programs_ = required_programs();
    for (paths_set::const_iterator iter = required_programs_.begin();
         iter != required_programs_.end(); iter++) {
        if ((*iter).is_absolute()) {
            if (!fs::exists(*iter))
                return F("Required program '%s' not found") % *iter;
        } else {
            if (!fs::find_in_path((*iter).c_str()))
                return F("Required program '%s' not found in PATH") % *iter;
        }
    }

    const units::bytes& required_memory_ = required_memory();
    if (required_memory_ > 0) {
        const units::bytes physical_memory = utils::physical_memory();
        if (physical_memory > 0 && physical_memory < required_memory_)
            return F("Requires %s bytes of physical memory but only %s "
                     "available") %
                required_memory_.format() % physical_memory.format();
    }

    return "";
}


/// Runs the test case in debug mode.
///
/// Debug mode gives the caller more control on the execution of the test.  It
/// should not be used for normal execution of tests; instead, call run().
///
/// \param test_case The test case to debug.
/// \param user_config The user configuration that defines the execution of this
///     test case.
/// \param hooks Hooks to introspect the execution of the test case.
/// \param stdout_path The file to which to redirect the stdout of the test.
///     For interactive debugging, '/dev/stdout' is probably a reasonable value.
/// \param stderr_path The file to which to redirect the stdout of the test.
///     For interactive debugging, '/dev/stderr' is probably a reasonable value.
///
/// \return The result of the execution of the test case.
engine::test_result
engine::atf_iface::debug_atf_test_case(const base_test_case* test_case,
                                       const config::tree& user_config,
                                       test_case_hooks& hooks,
                                       const fs::path& stdout_path,
                                       const fs::path& stderr_path)
{
    return execute(test_case, user_config, hooks,
                   utils::make_optional(stdout_path),
                   utils::make_optional(stderr_path));
}


/// Runs the test case.
///
/// \param test_case The test case to run.
/// \param user_config The user configuration that defines the execution of this
///     test case.
/// \param hooks Hooks to introspect the execution of the test case.
///
/// \return The result of the execution of the test case.
engine::test_result
engine::atf_iface::run_atf_test_case(const base_test_case* test_case,
                                     const config::tree& user_config,
                                     test_case_hooks& hooks)
{
    return execute(test_case, user_config, hooks, none, none);
}
