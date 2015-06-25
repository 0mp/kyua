// Copyright 2014 The Kyua Authors.
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

#include "engine/scheduler.hpp"

extern "C" {
#include <unistd.h>
}

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "engine/config.hpp"
#include "engine/exceptions.hpp"
#include "engine/requirements.hpp"
#include "model/context.hpp"
#include "model/metadata.hpp"
#include "model/test_case.hpp"
#include "model/test_program.hpp"
#include "model/test_result.hpp"
#include "utils/config/tree.ipp"
#include "utils/datetime.hpp"
#include "utils/defs.hpp"
#include "utils/env.hpp"
#include "utils/format/macros.hpp"
#include "utils/fs/directory.hpp"
#include "utils/fs/exceptions.hpp"
#include "utils/fs/operations.hpp"
#include "utils/fs/path.hpp"
#include "utils/logging/macros.hpp"
#include "utils/noncopyable.hpp"
#include "utils/optional.ipp"
#include "utils/passwd.hpp"
#include "utils/process/executor.ipp"
#include "utils/process/status.hpp"
#include "utils/sanity.hpp"
#include "utils/shared_ptr.hpp"
#include "utils/stacktrace.hpp"
#include "utils/stream.hpp"
#include "utils/text/operations.ipp"

namespace config = utils::config;
namespace datetime = utils::datetime;
namespace executor = utils::process::executor;
namespace fs = utils::fs;
namespace logging = utils::logging;
namespace passwd = utils::passwd;
namespace process = utils::process;
namespace scheduler = engine::scheduler;
namespace text = utils::text;

using utils::none;
using utils::optional;


/// Timeout for the test case cleanup operation.
///
/// TODO(jmmv): This is here only for testing purposes.  Maybe we should expose
/// this setting as part of the user_config.
datetime::delta scheduler::cleanup_timeout(60, 0);


/// Timeout for the test case listing operation.
///
/// TODO(jmmv): This is here only for testing purposes.  Maybe we should expose
/// this setting as part of the user_config.
datetime::delta scheduler::list_timeout(300, 0);


namespace {


/// Magic exit status to indicate that the test case was probably skipped.
///
/// The test case was only skipped if and only if we return this exit code and
/// we find the skipped_cookie file on disk.
static const int exit_skipped = 84;


/// Text file containing the skip reason for the test case.
///
/// This will only be present within unique_work_directory if the test case
/// exited with the exit_skipped code.  However, there is no guarantee that the
/// file is there (say if the test really decided to exit with code exit_skipped
/// on its own).
static const char* skipped_cookie = "skipped.txt";


/// Mapping of interface names to interface definitions.
typedef std::map< std::string, std::shared_ptr< scheduler::interface > >
    interfaces_map;


/// Mapping of interface names to interface definitions.
///
/// Use register_interface() to add an entry to this global table.
static interfaces_map interfaces;


/// Scans the contents of a directory and appends the file listing to a file.
///
/// \param dir_path The directory to scan.
/// \param output_file The file to which to append the listing.
///
/// \throw engine::error If there are problems listing the files.
static void
append_files_listing(const fs::path& dir_path, const fs::path& output_file)
{
    std::ofstream output(output_file.c_str(), std::ios::app);
    if (!output)
        throw engine::error(F("Failed to open output file %s for append")
                            % output_file);
    try {
        std::set < std::string > names;

        const fs::directory dir(dir_path);
        for (fs::directory::const_iterator iter = dir.begin();
             iter != dir.end(); ++iter) {
            if (iter->name != "." && iter->name != "..")
                names.insert(iter->name);
        }

        if (!names.empty()) {
            output << "Files left in work directory after failure: "
                   << text::join(names, ", ") << '\n';
        }
    } catch (const fs::error& e) {
        throw engine::error(F("Cannot append files listing to %s: %s")
                            % output_file % e.what());
    }
}


/// Maintenance data held while a test is being executed.
///
/// This data structure exists from the moment when a test is executed via
/// scheduler::spawn_test() to when it is cleaned up with
/// result_handle::cleanup().
struct exec_data : utils::noncopyable {
    /// Test program-specific execution interface.
    std::shared_ptr< scheduler::interface > interface;

    /// Test program data for this test case.
    model::test_program_ptr test_program;

    /// Name of the test case.
    std::string test_case_name;

    /// User configuration passed to the execution of the test.  We need this
    /// here to recover it later when chaining the execution of a cleanup
    /// routine (if any).
    const config::tree user_config;

    /// If not none, the exit handle of the test's body.  The presence of a
    /// value implies that this exec_data corresponds to a cleanup routine and
    /// not the test case per se.
    optional< executor::exit_handle > body_exit_handle;

    /// If not none, the final result of the test's body.  The presence of a
    /// value implies that this exec_data corresponds to a cleanup routine and
    /// not the test case per se.
    optional< model::test_result > body_result;

    /// Constructor.
    ///
    /// \param interface_ Test program-specific execution interface.
    /// \param test_program_ Test program data for this test case.
    /// \param test_case_name_ Name of the test case.
    /// \param user_config_ User configuration passed to the test.
    /// \param body_exit_handle_ If not none, exit handle of the body
    ///     corresponding to the cleanup routine represented by this exec_data.
    /// \param body_result_ If not none, result of the body corresponding to the
    ///     cleanup routine represented by this exec_data.
    exec_data(const std::shared_ptr< scheduler::interface >& interface_,
              const model::test_program_ptr test_program_,
              const std::string& test_case_name_,
              const config::tree& user_config_,
              const optional< executor::exit_handle >& body_exit_handle_,
              const optional< model::test_result >& body_result_) :
        interface(interface_), test_program(test_program_),
        test_case_name(test_case_name_), user_config(user_config_),
        body_exit_handle(body_exit_handle_), body_result(body_result_)
    {
    }
};


/// Shared pointer to exec_data.
///
/// We require this because we want exec_data to not be copyable, and thus we
/// cannot just store it in the map without move constructors.
typedef std::shared_ptr< exec_data > exec_data_ptr;


/// Mapping of active test case handles to their maintenance data.
typedef std::map< scheduler::exec_handle, exec_data_ptr > exec_data_map;


/// Enforces a test program to hold an absolute path.
///
/// TODO(jmmv): This function (which is a pretty ugly hack) exists because we
/// want the interface hooks to receive a test_program as their argument.
/// However, those hooks run after the test program has been isolated, which
/// means that the current directory has changed since when the test_program
/// objects were created.  This causes the absolute_path() method of
/// test_program to return bogus values if the internal representation of their
/// path is relative.  We should fix somehow: maybe making the fs module grab
/// its "current_path" view at program startup time; or maybe by grabbing the
/// current path at test_program creation time; or maybe something else.
///
/// \param program The test program to modify.
///
/// \return A new test program whose internal paths are absolute.
static model::test_program
force_absolute_paths(const model::test_program program)
{
    const std::string& relative = program.relative_path().str();
    const std::string absolute = program.absolute_path().str();

    const std::string root = absolute.substr(
        0, absolute.length() - relative.length());

    return model::test_program(
        program.interface_name(),
        program.relative_path(), fs::path(root),
        program.test_suite_name(),
        program.get_metadata(), program.test_cases());
}


/// Functor to list the test cases of a test program.
class list_test_cases {
    /// Interface of the test program to execute.
    std::shared_ptr< scheduler::interface > _interface;

    /// Test program to execute.
    const model::test_program _test_program;

    /// User-provided configuration variables.
    const config::tree& _user_config;

public:
    /// Constructor.
    ///
    /// \param interface Interface of the test program to execute.
    /// \param test_program Test program to execute.
    /// \param user_config User-provided configuration variables.
    list_test_cases(
        const std::shared_ptr< scheduler::interface > interface,
        const model::test_program* test_program,
        const config::tree& user_config) :
        _interface(interface),
        _test_program(force_absolute_paths(*test_program)),
        _user_config(user_config)
    {
    }

    /// Body of the subprocess.
    void
    operator()(const fs::path& UTILS_UNUSED_PARAM(control_directory))
    {
        const config::properties_map vars = scheduler::generate_config(
            _user_config, _test_program.test_suite_name());
        _interface->exec_list(_test_program, vars);
    }
};


/// Functor to execute a test program in a child process.
class run_test_program {
    /// Interface of the test program to execute.
    std::shared_ptr< scheduler::interface > _interface;

    /// Test program to execute.
    const model::test_program _test_program;

    /// Name of the test case to execute.
    const std::string& _test_case_name;

    /// User-provided configuration variables.
    const config::tree& _user_config;

    /// Verifies if the test case needs to be skipped or not.
    ///
    /// We could very well run this on the scheduler parent process before
    /// issuing the fork.  However, doing this here in the child process is
    /// better for two reasons: first, it allows us to continue using the simple
    /// spawn/wait abstraction of the scheduler; and, second, we parallelize the
    /// requirements checks among tests.
    ///
    /// \post If the test's preconditions are not met, the caller process is
    /// terminated with a special exit code and a "skipped cookie" is written to
    /// the disk with the reason for the failure.
    ///
    /// \param skipped_cookie_path File to create with the skip reason details
    ///     if this test is skipped.
    void
    do_requirements_check(const fs::path& skipped_cookie_path)
    {
        const model::test_case& test_case = _test_program.find(
            _test_case_name);

        const std::string skip_reason = engine::check_reqs(
            test_case.get_metadata(), _user_config,
            _test_program.test_suite_name(),
            fs::current_path());
        if (skip_reason.empty())
            return;

        std::ofstream output(skipped_cookie_path.c_str());
        if (!output) {
            std::perror((F("Failed to open %s for write") %
                         skipped_cookie_path).str().c_str());
            std::abort();
        }
        output << skip_reason;
        output.close();

        // Abruptly terminate the process.  We don't want to run any destructors
        // inherited from the parent process by mistake, which could, for
        // example, delete our own control files!
        ::_exit(exit_skipped);
    }

public:
    /// Constructor.
    ///
    /// \param interface Interface of the test program to execute.
    /// \param test_program Test program to execute.
    /// \param test_case_name Name of the test case to execute.
    /// \param user_config User-provided configuration variables.
    run_test_program(
        const std::shared_ptr< scheduler::interface > interface,
        const model::test_program_ptr test_program,
        const std::string& test_case_name,
        const config::tree& user_config) :
        _interface(interface),
        _test_program(force_absolute_paths(*test_program)),
        _test_case_name(test_case_name),
        _user_config(user_config)
    {
    }

    /// Body of the subprocess.
    void
    operator()(const fs::path& control_directory)
    {
        const model::test_case& test_case = _test_program.find(
            _test_case_name);
        if (test_case.fake_result())
            ::_exit(EXIT_SUCCESS);

        do_requirements_check(control_directory / skipped_cookie);

        const config::properties_map vars = scheduler::generate_config(
            _user_config, _test_program.test_suite_name());
        _interface->exec_test(_test_program, _test_case_name, vars,
                              control_directory);
    }
};


/// Functor to execute a test program in a child process.
class run_test_cleanup {
    /// Interface of the test program to execute.
    std::shared_ptr< scheduler::interface > _interface;

    /// Test program to execute.
    const model::test_program _test_program;

    /// Name of the test case to execute.
    const std::string& _test_case_name;

    /// User-provided configuration variables.
    const config::tree& _user_config;

public:
    /// Constructor.
    ///
    /// \param interface Interface of the test program to execute.
    /// \param test_program Test program to execute.
    /// \param test_case_name Name of the test case to execute.
    /// \param user_config User-provided configuration variables.
    run_test_cleanup(
        const std::shared_ptr< scheduler::interface > interface,
        const model::test_program_ptr test_program,
        const std::string& test_case_name,
        const config::tree& user_config) :
        _interface(interface),
        _test_program(force_absolute_paths(*test_program)),
        _test_case_name(test_case_name),
        _user_config(user_config)
    {
    }

    /// Body of the subprocess.
    void
    operator()(const fs::path& control_directory)
    {
        const config::properties_map vars = scheduler::generate_config(
            _user_config, _test_program.test_suite_name());
        _interface->exec_cleanup(_test_program, _test_case_name, vars,
                                 control_directory);
    }
};


/// Obtains the right scheduler interface for a given test program.
///
/// \param name The name of the interface of the test program.
///
/// \return An scheduler interface.
std::shared_ptr< scheduler::interface >
find_interface(const std::string& name)
{
    const interfaces_map::const_iterator iter = interfaces.find(name);
    PRE(interfaces.find(name) != interfaces.end());
    return (*iter).second;
}


}  // anonymous namespace


void
scheduler::interface::exec_cleanup(
    const model::test_program& UTILS_UNUSED_PARAM(test_program),
    const std::string& UTILS_UNUSED_PARAM(test_case_name),
    const utils::config::properties_map& UTILS_UNUSED_PARAM(vars),
    const utils::fs::path& UTILS_UNUSED_PARAM(control_directory)) const
{
    // Most test interfaces do not support standalone cleanup routines so
    // provide a default implementation that does nothing.
    UNREACHABLE_MSG("exec_cleanup not implemented for an interface that "
                    "supports standalone cleanup routines");
}


/// Internal implementation of a lazy_test_program.
struct engine::scheduler::lazy_test_program::impl : utils::noncopyable {
    /// Whether the test cases list has been yet loaded or not.
    bool _loaded;

    /// User configuration to pass to the test program list operation.
    config::tree _user_config;

    /// Scheduler context to use to load test cases.
    scheduler::scheduler_handle _scheduler_handle;

    /// Constructor.
    impl(const config::tree& user_config_,
         scheduler::scheduler_handle& scheduler_handle_) :
        _loaded(false), _user_config(user_config_),
        _scheduler_handle(scheduler_handle_)
    {
    }
};


/// Constructs a new test program.
///
/// \param interface_name_ Name of the test program interface.
/// \param binary_ The name of the test program binary relative to root_.
/// \param root_ The root of the test suite containing the test program.
/// \param test_suite_name_ The name of the test suite this program belongs to.
/// \param md_ Metadata of the test program.
/// \param user_config_ User configuration to pass to the scheduler.
/// \param scheduler_handle_ Scheduler context to use to load test cases.
scheduler::lazy_test_program::lazy_test_program(
    const std::string& interface_name_,
    const fs::path& binary_,
    const fs::path& root_,
    const std::string& test_suite_name_,
    const model::metadata& md_,
    const config::tree& user_config_,
    scheduler::scheduler_handle& scheduler_handle_) :
    test_program(interface_name_, binary_, root_, test_suite_name_, md_,
                 model::test_cases_map()),
    _pimpl(new impl(user_config_, scheduler_handle_))
{
}


/// Gets or loads the list of test cases from the test program.
///
/// \return The list of test cases provided by the test program.
const model::test_cases_map&
scheduler::lazy_test_program::test_cases(void) const
{
    _pimpl->_scheduler_handle.check_interrupt();

    if (!_pimpl->_loaded) {
        const model::test_cases_map tcs = _pimpl->_scheduler_handle.list_tests(
            this, _pimpl->_user_config);

        // Due to the restrictions on when set_test_cases() may be called (as a
        // way to lazily initialize the test cases list before it is ever
        // returned), this cast is valid.
        const_cast< scheduler::lazy_test_program* >(this)->set_test_cases(tcs);

        _pimpl->_loaded = true;

        _pimpl->_scheduler_handle.check_interrupt();
    }

    INV(_pimpl->_loaded);
    return test_program::test_cases();
}


/// Internal implementation for the result_handle class.
struct engine::scheduler::result_handle::bimpl : utils::noncopyable {
    /// Generic executor exit handle for this result handle.
    executor::exit_handle generic;

    /// Mutable pointer to the corresponding scheduler state.
    ///
    /// This object references a member of the scheduler_handle that yielded
    /// this result_handle instance.  We need this direct access to clean up
    /// after ourselves when the result is destroyed.
    exec_data_map& all_exec_data;

    /// Constructor.
    ///
    /// \param generic_ Generic executor exit handle for this result handle.
    /// \param [in,out] all_exec_data_ Global object keeping track of all active
    ///     executions for an scheduler.  This is a pointer to a member of the
    ///     scheduler_handle object.
    bimpl(const executor::exit_handle generic_, exec_data_map& all_exec_data_) :
        generic(generic_), all_exec_data(all_exec_data_)
    {
    }

    /// Destructor.
    ~bimpl(void)
    {
        all_exec_data.erase(generic.original_exec_handle());
    }
};


/// Constructor.
///
/// \param pbimpl Constructed internal implementation.
scheduler::result_handle::result_handle(std::shared_ptr< bimpl > pbimpl) :
    _pbimpl(pbimpl)
{
}


/// Destructor.
scheduler::result_handle::~result_handle(void)
{
}


/// Cleans up the test case results.
///
/// This function should be called explicitly as it provides the means to
/// control any exceptions raised during cleanup.  Do not rely on the destructor
/// to clean things up.
///
/// \throw engine::error If the cleanup fails, especially due to the inability
///     to remove the work directory.
void
scheduler::result_handle::cleanup(void)
{
    _pbimpl->generic.cleanup();
}


/// Returns the original exec_handle corresponding to this result.
///
/// \return An exec_handle.
scheduler::exec_handle
scheduler::result_handle::original_exec_handle(void) const
{
    return _pbimpl->generic.original_exec_handle();
}


/// Returns the timestamp of when spawn_test was called.
///
/// \return A timestamp.
const datetime::timestamp&
scheduler::result_handle::start_time(void) const
{
    return _pbimpl->generic.start_time();
}


/// Returns the timestamp of when wait_any_test returned this object.
///
/// \return A timestamp.
const datetime::timestamp&
scheduler::result_handle::end_time(void) const
{
    return _pbimpl->generic.end_time();
}


/// Returns the path to the test-specific work directory.
///
/// This is guaranteed to be clear of files created by the scheduler.
///
/// \return The path to a directory that exists until cleanup() is called.
fs::path
scheduler::result_handle::work_directory(void) const
{
    return _pbimpl->generic.work_directory();
}


/// Returns the path to the test's stdout file.
///
/// \return The path to a file that exists until cleanup() is called.
const fs::path&
scheduler::result_handle::stdout_file(void) const
{
    return _pbimpl->generic.stdout_file();
}


/// Returns the path to the test's stderr file.
///
/// \return The path to a file that exists until cleanup() is called.
const fs::path&
scheduler::result_handle::stderr_file(void) const
{
    return _pbimpl->generic.stderr_file();
}


/// Internal implementation for the test_result_handle class.
struct engine::scheduler::test_result_handle::impl : utils::noncopyable {
    /// Test program data for this test case.
    model::test_program_ptr test_program;

    /// Name of the test case.
    std::string test_case_name;

    /// The actual result of the test execution.
    const model::test_result test_result;

    /// Constructor.
    ///
    /// \param test_program_ Test program data for this test case.
    /// \param test_case_name_ Name of the test case.
    /// \param test_result_ The actual result of the test execution.
    impl(const model::test_program_ptr test_program_,
         const std::string& test_case_name_,
         const model::test_result& test_result_) :
        test_program(test_program_),
        test_case_name(test_case_name_),
        test_result(test_result_)
    {
    }
};


/// Constructor.
///
/// \param pbimpl Constructed internal implementation for the base object.
/// \param pimpl Constructed internal implementation.
scheduler::test_result_handle::test_result_handle(
    std::shared_ptr< bimpl > pbimpl, std::shared_ptr< impl > pimpl) :
    result_handle(pbimpl), _pimpl(pimpl)
{
}


/// Destructor.
scheduler::test_result_handle::~test_result_handle(void)
{
}


/// Returns the test program that yielded this result.
///
/// \return A test program.
const model::test_program_ptr
scheduler::test_result_handle::test_program(void) const
{
    return _pimpl->test_program;
}


/// Returns the name of the test case that yielded this result.
///
/// \return A test case name
const std::string&
scheduler::test_result_handle::test_case_name(void) const
{
    return _pimpl->test_case_name;
}


/// Returns the actual result of the test execution.
///
/// \return A test result.
const model::test_result&
scheduler::test_result_handle::test_result(void) const
{
    return _pimpl->test_result;
}


/// Internal implementation for the scheduler_handle.
struct engine::scheduler::scheduler_handle::impl : utils::noncopyable {
    /// Generic executor instance encapsulated by this one.
    executor::executor_handle generic;

    /// Mapping of exec handles to the data required at run time.
    exec_data_map all_exec_data;

    /// Constructor.
    impl(void) : generic(executor::setup())
    {
    }

    /// Destructor.
    ~impl(void)
    {
    }
};


/// Constructor.
scheduler::scheduler_handle::scheduler_handle(void) throw() : _pimpl(new impl())
{
}


/// Destructor.
scheduler::scheduler_handle::~scheduler_handle(void)
{
}


/// Queries the path to the root of the work directory for all tests.
///
/// \return A path.
const fs::path&
scheduler::scheduler_handle::root_work_directory(void) const
{
    return _pimpl->generic.root_work_directory();
}


/// Cleans up the scheduler state.
///
/// This function should be called explicitly as it provides the means to
/// control any exceptions raised during cleanup.  Do not rely on the destructor
/// to clean things up.
///
/// \throw engine::error If there are problems cleaning up the scheduler.
void
scheduler::scheduler_handle::cleanup(void)
{
    _pimpl->generic.cleanup();
}


/// Checks if the given interface name is valid.
///
/// \param interface The name of the interface to validate.
///
/// \throw engine::error If the given interface is not supported.
void
scheduler::ensure_valid_interface(const std::string& name)
{
    if (interfaces.find(name) == interfaces.end())
        throw engine::error(F("Unsupported test interface '%s'") % name);
}


/// Registers a new interface.
///
/// \param name The name of the interface.  Must not have yet been registered.
/// \param spec Interface specification.
void
scheduler::register_interface(const std::string& name,
                              const std::shared_ptr< interface > spec)
{
    PRE(interfaces.find(name) == interfaces.end());
    interfaces.insert(interfaces_map::value_type(name, spec));
}


/// Returns the names of all registered interfaces.
///
/// \return A collection of interface names.
std::set< std::string >
scheduler::registered_interface_names(void)
{
    std::set< std::string > names;
    for (interfaces_map::const_iterator iter = interfaces.begin();
         iter != interfaces.end(); ++iter) {
        names.insert((*iter).first);
    }
    return names;
}


/// Initializes the scheduler.
///
/// \pre This function can only be called if there is no other scheduler_handle
/// object alive.
///
/// \return A handle to the operations of the scheduler.
scheduler::scheduler_handle
scheduler::setup(void)
{
    return scheduler_handle();
}


/// Retrieves the list of test cases from a test program.
///
/// This operation is currently synchronous.
///
/// This operation should never throw.  Any errors during the processing of the
/// test case list are subsumed into a single test case in the return value that
/// represents the failed retrieval.
///
/// \param test_program The test program from which to obtain the list of test
/// cases.
/// \param user_config User-provided configuration variables.
///
/// \return The list of test cases.
model::test_cases_map
scheduler::scheduler_handle::list_tests(
    const model::test_program* test_program,
    const config::tree& user_config)
{
    _pimpl->generic.check_interrupt();

    const std::shared_ptr< scheduler::interface > interface = find_interface(
        test_program->interface_name());

    try {
        const executor::exec_handle exec_handle = _pimpl->generic.spawn(
            list_test_cases(interface, test_program, user_config),
            list_timeout, none);
        executor::exit_handle exit_handle = _pimpl->generic.wait(exec_handle);

        const model::test_cases_map test_cases = interface->parse_list(
            exit_handle.status(),
            exit_handle.stdout_file(),
            exit_handle.stderr_file());

        exit_handle.cleanup();

        if (test_cases.empty())
            throw std::runtime_error("Empty test cases list");

        return test_cases;
    } catch (const std::runtime_error& e) {
        // TODO(jmmv): This is a very ugly workaround for the fact that we
        // cannot report failures at the test-program level.
        LW(F("Failed to load test cases list: %s") % e.what());
        model::test_cases_map fake_test_cases;
        fake_test_cases.insert(model::test_cases_map::value_type(
            "__test_cases_list__",
            model::test_case(
                "__test_cases_list__",
                "Represents the correct processing of the test cases list",
                model::test_result(model::test_result_broken, e.what()))));
        return fake_test_cases;
    }
}


/// Forks and executes a test case asynchronously.
///
/// Note that the caller needn't know if the test has a cleanup routine or not.
/// If there indeed is a cleanup routine, we trigger it at wait_any() time.
///
/// \param test_program The container test program.
/// \param test_case_name The name of the test case to run.
/// \param user_config User-provided configuration variables.
/// \param stdout_target If not none, file to which to write the stdout of the
///     test case.
/// \param stderr_target If not none, file to which to write the stderr of the
///     test case.
///
/// \return A handle for the background operation.  Used to match the result of
/// the execution returned by wait_any() with this invocation.
scheduler::exec_handle
scheduler::scheduler_handle::spawn_test(
    const model::test_program_ptr test_program,
    const std::string& test_case_name,
    const config::tree& user_config,
    const optional< fs::path > stdout_target,
    const optional< fs::path > stderr_target)
{
    _pimpl->generic.check_interrupt();

    const std::shared_ptr< scheduler::interface > interface = find_interface(
        test_program->interface_name());

    LI(F("Spawning %s:%s") % test_program->absolute_path() % test_case_name);

    const model::test_case& test_case = test_program->find(test_case_name);

    optional< passwd::user > unprivileged_user;
    if (user_config.is_set("unprivileged_user") &&
        test_case.get_metadata().required_user() == "unprivileged") {
        unprivileged_user = user_config.lookup< engine::user_node >(
            "unprivileged_user");
    }

    const executor::exec_handle handle = _pimpl->generic.spawn(
        run_test_program(interface, test_program, test_case_name,
                         user_config),
        test_case.get_metadata().timeout(),
        unprivileged_user, stdout_target, stderr_target);

    const exec_data_ptr data(new exec_data(
        interface, test_program, test_case_name, user_config, none, none));
    _pimpl->all_exec_data.insert(exec_data_map::value_type(handle, data));

    return handle;
}


/// Forks and executes a test case cleanup routine asynchronously.
///
/// \param test_program The container test program.
/// \param test_case_name The name of the test case to run.
/// \param user_config User-provided configuration variables.
/// \param body_handle The exit handle of the test case's corresponding body.
///     The cleanup will be executed in the same context.
/// \param body_result The result of the test case's corresponding body.
///
/// \return A handle for the background operation.  Used to match the result of
/// the execution returned by wait_any() with this invocation.
scheduler::exec_handle
scheduler::scheduler_handle::spawn_cleanup(
    const model::test_program_ptr test_program,
    const std::string& test_case_name,
    const config::tree& user_config,
    const executor::exit_handle& body_handle,
    const model::test_result& body_result)
{
    _pimpl->generic.check_interrupt();

    const std::shared_ptr< scheduler::interface > interface = find_interface(
        test_program->interface_name());

    LI(F("Spawning %s:%s (cleanup)") % test_program->absolute_path() %
       test_case_name);

    const model::test_case& test_case = test_program->find(test_case_name);

    optional< passwd::user > unprivileged_user;
    if (user_config.is_set("unprivileged_user") &&
        test_case.get_metadata().required_user() == "unprivileged") {
        unprivileged_user = user_config.lookup< engine::user_node >(
            "unprivileged_user");
    }

    const executor::exec_handle handle = _pimpl->generic.spawn_followup(
        run_test_cleanup(interface, test_program, test_case_name, user_config),
        body_handle, cleanup_timeout);

    const exec_data_ptr data(new exec_data(
        interface, test_program, test_case_name, user_config,
        utils::make_optional(body_handle),
        utils::make_optional(body_result)));
    _pimpl->all_exec_data.insert(exec_data_map::value_type(handle, data));

    return handle;
}


/// Waits for completion of any forked test case.
///
/// Note that if the terminated test case has a cleanup routine, this function
/// is the one in charge of spawning the cleanup routine asynchronously.
///
/// \return The result of the execution of a subprocess.  This is a dynamically
/// allocated object because the scheduler can spawn subprocesses of various
/// types and, at wait time, we don't know upfront what we are going to get.
scheduler::result_handle_ptr
scheduler::scheduler_handle::wait_any(void)
{
    _pimpl->generic.check_interrupt();

    executor::exit_handle handle = _pimpl->generic.wait_any();

    const exec_data_map::iterator iter = _pimpl->all_exec_data.find(
        handle.original_exec_handle());
    exec_data_ptr& data = (*iter).second;

    utils::dump_stacktrace_if_available(data->test_program->absolute_path(),
                                        _pimpl->generic, handle);

    optional< model::test_result > result;
    if (!data->body_result) {
        const model::test_case& test_case = data->test_program->find(
            data->test_case_name);

        result = test_case.fake_result();

        if (!result && handle.status() && handle.status().get().exited() &&
            handle.status().get().exitstatus() == exit_skipped) {
            // If the test's process terminated with our magic "exit_skipped"
            // status, there are two cases to handle.  The first is the case
            // where the "skipped cookie" exists, in which case we never got to
            // actually invoke the test program; if that's the case, handle it
            // here.  The second case is where the test case actually decided to
            // exit with the "exit_skipped" status; in that case, just fall back
            // to the regular status handling.
            const fs::path skipped_cookie_path = handle.control_directory() /
                skipped_cookie;
            std::ifstream input(skipped_cookie_path.c_str());
            if (input) {
                result = model::test_result(model::test_result_skipped,
                                            utils::read_stream(input));
                input.close();
            }
        }
        if (!result) {
            result = data->interface->compute_result(
                handle.status(),
                handle.control_directory(),
                handle.stdout_file(),
                handle.stderr_file());
        }
        INV(result);

        if (!result.get().good()) {
            append_files_listing(handle.work_directory(),
                                 handle.stderr_file());
        }

        if (test_case.get_metadata().has_cleanup()) {
            // The test body has completed and we have processed it.  If there
            // is a cleanup routine, trigger it now and wait for any other test
            // completion.  The caller never knows about cleanup routines.
            spawn_cleanup(data->test_program, data->test_case_name,
                          data->user_config, handle, result.get());

            // TODO(jmmv): Chaining this call is ugly.  We'd be better off by
            // looping over terminated processes until we got a result suitable
            // for user consumption.  For the time being this is good enough and
            // not a problem because the call chain won't get big: the majority
            // of test cases do not have cleanup routines.
            return wait_any();
        }
    } else {
        // If there is a "body result" available, it means we just finished
        // executing a cleanup routine for a previously-executed test case.
        // Handle it here: cleanup failures are only reported if the body exited
        // successfully; otherwise we rely on the body result to report the test
        // failure.
        const model::test_result& body_result = data->body_result.get();
        if (body_result.good()) {
            if (!handle.status()) {
                result = model::test_result(model::test_result_broken,
                                            "Test case cleanup timed out");
            } else {
                if (!handle.status().get().exited() ||
                    handle.status().get().exitstatus() != EXIT_SUCCESS) {
                    result = model::test_result(
                        model::test_result_broken,
                        "Test case cleanup did not terminate successfully");
                } else {
                    result = body_result;
                }
            }
        } else {
            result = body_result;
        }

        // Return the original exit_handle to the caller, because the caller is
        // unaware that we executed a cleanup routine under the hood.  This is
        // necessary because the caller can use the original_exec_handle() for
        // tracking purposes.
        handle = data->body_exit_handle.get();
    }
    INV(result);

    std::shared_ptr< result_handle::bimpl > result_handle_bimpl(
        new result_handle::bimpl(handle, _pimpl->all_exec_data));
    std::shared_ptr< test_result_handle::impl > test_result_handle_impl(
        new test_result_handle::impl(
            data->test_program, data->test_case_name, result.get()));
    return result_handle_ptr(new test_result_handle(result_handle_bimpl,
                                                    test_result_handle_impl));
}


/// Checks if an interrupt has fired.
///
/// Calls to this function should be sprinkled in strategic places through the
/// code protected by an interrupts_handler object.
///
/// This is just a wrapper over signals::check_interrupt() to avoid leaking this
/// dependency to the caller.
///
/// \throw signals::interrupted_error If there has been an interrupt.
void
scheduler::scheduler_handle::check_interrupt(void) const
{
    _pimpl->generic.check_interrupt();
}


/// Queries the current execution context.
///
/// \return The queried context.
model::context
scheduler::current_context(void)
{
    return model::context(fs::current_path(), utils::getallenv());
}


/// Generates the set of configuration variables for a test program.
///
/// \param user_config The configuration variables provided by the user.
/// \param test_suite The name of the test suite.
///
/// \return The mapping of configuration variables for the test program.
config::properties_map
scheduler::generate_config(const config::tree& user_config,
                           const std::string& test_suite)
{
    config::properties_map props;

    try {
        props = user_config.all_properties(F("test_suites.%s") % test_suite,
                                           true);
    } catch (const config::unknown_key_error& unused_error) {
        // Ignore: not all test suites have entries in the configuration.
    }

    // TODO(jmmv): This is a hack that exists for the ATF interface only, so it
    // should be moved there.
    if (user_config.is_set("unprivileged_user")) {
        const passwd::user& user =
            user_config.lookup< engine::user_node >("unprivileged_user");
        props["unprivileged-user"] = user.name;
    }

    return props;
}
