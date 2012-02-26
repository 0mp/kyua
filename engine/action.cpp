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

#include "engine/action.hpp"
#include "engine/context.hpp"

namespace fs = utils::fs;


/// Internal implementation of an action.
struct engine::action::impl {
    /// The runtime context of the action.
    const context& _context;

    /// Constructor.
    ///
    /// \param context_ The runtime context.
    impl(const context& context_) :
        _context(context_)
    {
    }
};


/// Constructs a new action.
///
/// \param context_ The runtime context in which the action runs.
engine::action::action(const context& context_) :
    _pimpl(new impl(context_))
{
}


/// Destructor.
engine::action::~action(void)
{
}


/// Returns a unique memory address for this action.
///
/// Remember that action objects are shallowly copied; therefore, it is possible
/// for two distinct variables of a context to return the same unique internal
/// address (which is perfectly okay).
///
/// \return The uniquely-identifying address for this action.
intptr_t
engine::action::unique_address(void) const
{
    return reinterpret_cast< intptr_t >(_pimpl.get());
}


/// Returns the context attached to this action.
///
/// \return A reference to the context.
const engine::context&
engine::action::runtime_context(void) const
{
    return _pimpl->_context;
}
