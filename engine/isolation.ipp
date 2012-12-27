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

extern "C" {
#include <signal.h>
}

#include <cerrno>

#include "engine/exceptions.hpp"
#include "engine/isolation.hpp"
#include "engine/test_result.hpp"
#include "utils/format/macros.hpp"
#include "utils/fs/auto_cleaners.hpp"
#include "utils/fs/exceptions.hpp"
#include "utils/logging/macros.hpp"
#include "utils/process/children.ipp"
#include "utils/process/exceptions.hpp"
#include "utils/signals/programmer.hpp"


/// Forks a subprocess and waits for its completion.
///
/// \pre This function can only be used in the context of a hook executed by
/// protected_run().
///
/// \param hook The code to execute in the subprocess.
/// \param outfile The file that will receive the stdout output.
/// \param errfile The file that will receive the stderr output.
/// \param timeout The amount of time given to the subprocess to execute.
///
/// \return The exit status of the process or none if the timeout expired.
template< class Hook >
utils::optional< utils::process::status >
engine::fork_and_wait(Hook hook, const utils::fs::path& outfile,
                      const utils::fs::path& errfile,
                      const utils::datetime::delta& timeout)
{
    std::auto_ptr< utils::process::child_with_files > child =
        utils::process::child_with_files::fork(hook, outfile, errfile);
    try {
        return utils::make_optional(child->wait(timeout));
    } catch (const utils::process::system_error& error) {
        if (error.original_errno() == EINTR) {
            (void)::kill(child->pid(), SIGKILL);
            (void)child->wait();
            check_interrupt();
            UNREACHABLE_MSG("fork_and_wait not wrapped by protected_run");
        } else
            throw error;
    } catch (const utils::process::timeout_error& error) {
        return utils::none;
    }
}


/// Auxiliary function to execute a test case.
///
/// This is an auxiliary function for run_test_case that is protected from
/// leaking exceptions.  Any exception not managed here is probably a mistake,
/// but is correctly captured in the caller.
///
/// \param test_case The test case to execute.
///
/// \return The result of the execution of the test case.
///
/// \throw interrupted_error If the execution has been interrupted by the user.
template< class Hook >
engine::test_result
engine::protected_run(Hook hook)
{
    // These three separate objects are ugly.  Maybe improve in some way.
    utils::signals::programmer sighup(SIGHUP, detail::interrupt_handler);
    utils::signals::programmer sigint(SIGINT, detail::interrupt_handler);
    utils::signals::programmer sigterm(SIGTERM, detail::interrupt_handler);

    test_result result(test_result::broken, "Test result not yet initialized");

    utils::fs::auto_directory workdir(create_work_directory());
    try {
        check_interrupt();
        result = hook(workdir.directory());
        try {
            workdir.cleanup();
        } catch (const utils::fs::error& e) {
            if (result.good()) {
                result = test_result(test_result::broken,
                    F("Could not clean up test work directory: %s") % e.what());
            } else {
                LW(F("Not reporting work directory clean up failure because "
                     "the test is already broken: %s") % e.what());
            }
        }
    } catch (const engine::interrupted_error& e) {
        workdir.cleanup();

        sighup.unprogram();
        sigint.unprogram();
        sigterm.unprogram();

        throw e;
    }

    sighup.unprogram();
    sigint.unprogram();
    sigterm.unprogram();

    check_interrupt();

    return result;
}
