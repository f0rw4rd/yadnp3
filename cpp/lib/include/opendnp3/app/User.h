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
#ifndef OPENDNP3_USER_H
#define OPENDNP3_USER_H

#include <cstdint>

namespace opendnp3
{

/**
 * A Secure Authentication user, by its user number. Without SA, every request is the default user's.
 */
class User
{
public:
    /// the user number IEEE 1815 reserves for the default user, "Common"
    static uint16_t DefaultId()
    {
        return 1;
    }

    /// user number 0 is no user
    static uint16_t UnknownId()
    {
        return 0;
    }

    User() : id(UnknownId()) {}

    explicit User(uint16_t id) : id(id) {}

    static User Default()
    {
        return User(DefaultId());
    }

    uint16_t GetId() const
    {
        return id;
    }

    bool IsDefault() const
    {
        return id == DefaultId();
    }

    bool IsUnknown() const
    {
        return id == UnknownId();
    }

    bool operator==(const User& other) const
    {
        return id == other.id;
    }

    bool operator!=(const User& other) const
    {
        return id != other.id;
    }

private:
    uint16_t id;
};

} // namespace opendnp3

#endif
