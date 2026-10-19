/*
 * Copyright 2013-2022 Step Function I/O, LLC
 *
 * Licensed to Green Energy Corp (www.greenenergycorp.com) and Step Function I/O
 * LLC (https://stepfunc.io) under one or more contributor license agreements.
 * See the NOTICE file distributed with this work for additional information
 * regarding copyright ownership. Green Energy Corp and Step Function I/O LLC license
 * this file to you under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License. You may obtain
 * a copy of the License at:
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef YADNP3_PYTHON_GIL_SAFE_H
#define YADNP3_PYTHON_GIL_SAFE_H

#include <pybind11/pybind11.h>

#include <memory>
#include <utility>

namespace py = pybind11;

/**
 * The shared_ptr to hand the stack for a handler that may be a Python subclass. Called with the GIL held.
 *
 * - It keeps the Python object alive as long as the stack holds the handler: pybind11's shared_ptr holder keeps only
 *   the C++ part alive, so once Python dropped its last reference, the overrides were gone and the stack's calls hit
 *   the base class.
 * - It releases the Python object with the GIL held, as the last reference often goes on an ASIO thread.
 */
template<typename T> std::shared_ptr<T> gil_safe_shared(std::shared_ptr<T> ptr)
{
    if (!ptr)
        return ptr;

    // the Python object of the handler, if it has one
    py::object self = py::cast(ptr.get(), py::return_value_policy::reference);
    auto held = std::make_shared<std::pair<std::shared_ptr<T>, py::object>>(std::move(ptr), std::move(self));
    T* raw = held->first.get();
    return std::shared_ptr<T>(raw, [held](T*) mutable {
        if (Py_IsInitialized())
        {
            py::gil_scoped_acquire gil;
            held.reset();
        }
        else
        {
            // the interpreter is gone, and with it any way to release the Python object
            new std::shared_ptr<std::pair<std::shared_ptr<T>, py::object>>(std::move(held));
        }
    });
}

/**
 * PYBIND11_OVERRIDE_PURE, with the GIL held until the end of the enclosing scope. When the Python class lacks the
 * method, the macro fails after its own GIL scope ended, and the failure needs the GIL: called on an ASIO thread, that
 * crashed the process.
 */
// the leading parameters are named because MSVC's traditional preprocessor passes a forwarded __VA_ARGS__ on as one
// argument, which then lands in ret_type
#define YADNP3_OVERRIDE_PURE(ret_type, cname, fn, ...)                                                                 \
    py::gil_scoped_acquire yadnp3_override_gil;                                                                        \
    PYBIND11_OVERRIDE_PURE(ret_type, cname, fn, __VA_ARGS__)

/**
 * Deletes an object whose destructor waits for the stack's threads with the GIL released: those threads may be
 * waiting for the GIL to call into Python.
 */
template<typename T> struct GilReleasingDeleter
{
    void operator()(T* ptr) const
    {
        if (Py_IsInitialized() && PyGILState_Check())
        {
            py::gil_scoped_release release;
            delete ptr;
        }
        else
        {
            delete ptr;
        }
    }
};

#endif
