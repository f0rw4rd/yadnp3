/*
 * Copyright 2013-2022 Step Function I/O, LLC
 * Copyright 2024-2026 f0rw4rd (experimental fork)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "utils/BufferHelpers.h"
#include "utils/CommandCallbackQueue.h"
#include "utils/MasterTestFixture.h"

#include <master/MasterSchedulerBackend.h>
#include <secauth/master/MasterAuthContext.h>

#include <opendnp3/secauth/IMasterApplicationSA.h>

#include <catch.hpp>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace opendnp3;

#define SUITE(name) "MasterAuthTestSuite - " name

// The test plays the outstation. It checks the master's messages with OpenSSL directly, not with the library's
// Secure Authentication code: it unwraps the session keys the master sends, and computes the HMACs itself.

namespace
{

using Bytes = std::vector<uint8_t>;

class MockMasterApplicationSA final : public IMasterApplicationSA
{
public:
    UTCTimestamp Now() override
    {
        return UTCTimestamp(0);
    }

    void OnTaskComplete(const TaskInfo& info) override
    {
        completions.push_back(info);
    }

    void PersistNewUpdateKey(const std::string&, uint16_t, const UpdateKey&) override {}

    std::vector<TaskInfo> completions;
};

Bytes FromHex(const std::string& hex)
{
    HexSequence seq(hex);
    const auto view = seq.ToRSeq();
    const uint8_t* bytes = view;
    return Bytes(bytes, bytes + view.length());
}

std::string ToHex(const Bytes& bytes)
{
    std::string ret;
    char buffer[4];
    for (const auto b : bytes)
    {
        std::snprintf(buffer, sizeof(buffer), ret.empty() ? "%02X" : " %02X", b);
        ret += buffer;
    }
    return ret;
}

Bytes Concat(std::initializer_list<Bytes> parts)
{
    Bytes ret;
    for (const auto& part : parts)
    {
        ret.insert(ret.end(), part.begin(), part.end());
    }
    return ret;
}

Bytes UInt16(uint16_t value)
{
    return Bytes{static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
}

Bytes UInt32(uint32_t value)
{
    return Bytes{static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 16),
                 static_cast<uint8_t>(value >> 24)};
}

// RFC 3394 key unwrap with the EVP interface
Bytes Unwrap(const Bytes& kek, const Bytes& wrapped)
{
    auto ctx = EVP_CIPHER_CTX_new();
    EVP_CIPHER_CTX_set_flags(ctx, EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);
    const auto cipher = (kek.size() == 16) ? EVP_aes_128_wrap() : EVP_aes_256_wrap();
    REQUIRE(EVP_DecryptInit_ex(ctx, cipher, nullptr, kek.data(), nullptr) == 1);
    Bytes out(wrapped.size());
    int len = 0;
    REQUIRE(EVP_DecryptUpdate(ctx, out.data(), &len, wrapped.data(), static_cast<int>(wrapped.size())) == 1);
    int final_len = 0;
    REQUIRE(EVP_DecryptFinal_ex(ctx, out.data() + len, &final_len) == 1);
    EVP_CIPHER_CTX_free(ctx);
    out.resize(static_cast<size_t>(len + final_len));
    return out;
}

// HMAC-SHA-256 over the data, truncated
Bytes HMAC256(const Bytes& key, const Bytes& data, size_t truncation)
{
    uint8_t out[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    REQUIRE(HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), data.data(), data.size(), out, &len)
            != nullptr);
    return Bytes(out, out + truncation);
}

// a key status object (g120v5), with the HMAC algorithm SHA-256 truncated to 16
Bytes KeyStatusObject(uint32_t ksq, KeyStatus status, const Bytes& challenge, const Bytes& hmac)
{
    return Concat({UInt32(ksq), UInt16(1), {0x01 /* AES-128 */, static_cast<uint8_t>(status), 0x04},
                   UInt16(static_cast<uint16_t>(challenge.size())), challenge, hmac});
}

// an authentication response with one free-format object of the group and variation
Bytes AuthResponse(uint8_t seq, uint8_t variation, const Bytes& object)
{
    return Concat({{static_cast<uint8_t>(0xC0 | seq), 0x83, 0x00, 0x00, 0x78, variation, 0x5B, 0x01},
                   UInt16(static_cast<uint16_t>(object.size())), object});
}

struct AuthFixture
{
    explicit AuthFixture(const MasterParams& params = NoStartupTasks())
        : exe(std::make_shared<exe4cpp::MockExecutor>()),
          meas(std::make_shared<MockSOEHandler>()),
          lower(std::make_shared<MockLowerLayer>()),
          application(std::make_shared<MockMasterApplicationSA>()),
          scheduler(std::make_shared<MasterSchedulerBackend>(exe)),
          context(MAuthContext::Create(Addresses(1, 1024),
                                       Logger(nullptr, ModuleId(), "test", LogLevels::everything()),
                                       exe,
                                       lower,
                                       meas,
                                       application,
                                       scheduler,
                                       params,
                                       MasterAuthSettings()))
    {
        lower->SetUpperLayer(*context);
    }

    ~AuthFixture()
    {
        scheduler->Shutdown();
        exe->clear();
    }

    Bytes PopWrite()
    {
        REQUIRE(lower->NumWrites() > 0);
        auto ret = FromHex(lower->PopWriteAsHex());
        context->OnTxReady();
        return ret;
    }

    void Send(const Bytes& apdu)
    {
        lower->SendUp(ToHex(apdu), Addresses(1, 1024).Reverse());
        exe->run_many();
    }

    // runs the session key exchange as the outstation, and returns the master's control session key
    Bytes SetSessionKeys(const Bytes& challenge = {0xAA, 0xBB, 0xCC, 0xDD}, bool goodHMAC = true)
    {
        REQUIRE(ToHex(PopWrite()) == "C0 20 78 04 07 01 01 00");

        const auto status = KeyStatusObject(5, KeyStatus::NOT_INIT, challenge, {});
        Send(AuthResponse(0, 5, status));

        // the key change: the object header, then KSQ, the user and the wrapped keys
        const auto change = PopWrite();
        REQUIRE(ToHex(Bytes(change.begin(), change.begin() + 8))
                == "C1 20 78 06 5B 01 " + ToHex(UInt16(static_cast<uint16_t>(change.size() - 8))));
        REQUIRE(ToHex(Bytes(change.begin() + 8, change.begin() + 14)) == "05 00 00 00 01 00");

        // the wrapped data: the key length, the control and monitor keys, the key status as the outstation sent it
        const auto unwrapped = Unwrap(updateKey, Bytes(change.begin() + 14, change.end()));
        REQUIRE(unwrapped.size() % 8 == 0);
        REQUIRE(ToHex(Bytes(unwrapped.begin(), unwrapped.begin() + 2)) == "10 00");
        const Bytes controlKey(unwrapped.begin() + 2, unwrapped.begin() + 18);
        const Bytes monitorKey(unwrapped.begin() + 18, unwrapped.begin() + 34);
        REQUIRE(controlKey != monitorKey);
        REQUIRE(Bytes(unwrapped.begin() + 34, unwrapped.begin() + 34 + status.size()) == status);

        // the key status that answers it has the HMAC, with the monitor key, of the whole key change message
        auto hmac = HMAC256(monitorKey, change, 16);
        if (!goodHMAC)
        {
            hmac[0] ^= 0xFF;
        }
        Send(AuthResponse(1, 5, KeyStatusObject(6, KeyStatus::OK, {0x01, 0x02, 0x03, 0x04}, hmac)));
        return controlKey;
    }

    void AddDefaultUser()
    {
        context->AddUser(User::Default(), "Common", UpdateKey(Buffer(updateKey.data(), updateKey.size())));
    }

    const Bytes updateKey = Bytes(16, 0x11);
    const std::shared_ptr<exe4cpp::MockExecutor> exe;
    const std::shared_ptr<MockSOEHandler> meas;
    const std::shared_ptr<MockLowerLayer> lower;
    const std::shared_ptr<MockMasterApplicationSA> application;
    const std::shared_ptr<MasterSchedulerBackend> scheduler;
    const std::shared_ptr<MAuthContext> context;
};

} // namespace

TEST_CASE(SUITE("Session keys are set for each user when the master comes online"))
{
    AuthFixture t;
    t.AddDefaultUser();
    REQUIRE(t.context->GetSessionKeyStatus(User::Default()) == KeyStatus::NOT_INIT);

    t.context->OnLowerLayerUp();
    t.exe->run_many();
    t.SetSessionKeys();

    REQUIRE(t.context->GetSessionKeyStatus(User::Default()) == KeyStatus::OK);
    REQUIRE(t.application->completions.back().type == MasterTaskType::SET_SESSION_KEYS);
    REQUIRE(t.application->completions.back().result == TaskCompletion::SUCCESS);
}

TEST_CASE(SUITE("A key status with a bad HMAC sets no session keys, and the master tries again"))
{
    AuthFixture t;
    t.AddDefaultUser();
    t.context->OnLowerLayerUp();
    t.exe->run_many();
    t.SetSessionKeys({0xAA, 0xBB, 0xCC, 0xDD}, false);

    REQUIRE(t.context->GetSessionKeyStatus(User::Default()) == KeyStatus::NOT_INIT);
    REQUIRE(t.application->completions.back().result == TaskCompletion::FAILURE_BAD_RESPONSE);

    // after the retry delay, the master asks for the key status again
    REQUIRE(t.lower->NumWrites() == 0);
    t.exe->advance_time(NoStartupTasks().taskRetryPeriod.value);
    t.exe->run_many();
    REQUIRE(ToHex(t.PopWrite()) == "C2 20 78 04 07 01 01 00");
}

TEST_CASE(SUITE("A key status for another user is refused"))
{
    AuthFixture t;
    t.AddDefaultUser();
    t.context->OnLowerLayerUp();
    t.exe->run_many();
    REQUIRE(ToHex(t.PopWrite()) == "C0 20 78 04 07 01 01 00");

    auto status = KeyStatusObject(5, KeyStatus::NOT_INIT, {0xAA, 0xBB, 0xCC, 0xDD}, {});
    status[4] = 0x02; // user 2
    t.Send(AuthResponse(0, 5, status));

    REQUIRE(t.lower->NumWrites() == 0);
    REQUIRE(t.application->completions.back().result == TaskCompletion::FAILURE_BAD_RESPONSE);
}

TEST_CASE(SUITE("A challenged control is answered with the HMAC of the challenge and the request"))
{
    AuthFixture t;
    t.AddDefaultUser();
    t.context->OnLowerLayerUp();
    t.exe->run_many();
    const auto controlKey = t.SetSessionKeys();

    CommandCallbackQueue queue;
    t.context->DirectOperate(CommandSet({WithIndex(ControlRelayOutputBlock(OperationType::PULSE_ON), 1)}),
                             queue.Callback(), TaskConfig::Default());
    t.exe->run_many();
    const auto request = t.PopWrite();
    REQUIRE(request[1] == 0x05);
    const auto seq = static_cast<uint8_t>(request[0] & 0x0F);

    // the outstation challenges it: CSQ 7, user 1, SHA-256 truncated to 16, reason critical, 4 bytes of challenge
    const auto challengeObject = Concat({UInt32(7), UInt16(1), {0x04, 0x01}, {0x10, 0x20, 0x30, 0x40}});
    const auto challenge = AuthResponse(seq, 1, challengeObject);
    t.Send(challenge);

    // the reply: CSQ, user and the HMAC, with the control key, of the challenge message and the request
    const auto reply = t.PopWrite();
    const auto hmac = HMAC256(controlKey, Concat({challenge, request}), 16);
    REQUIRE(ToHex(reply)
            == ToHex(Concat({{static_cast<uint8_t>(0xC0 | seq), 0x20, 0x78, 0x02, 0x5B, 0x01}, UInt16(22),
                             UInt32(7), UInt16(1), hmac})));

    // the outstation accepts it and answers the control
    const auto echo = Concat({{static_cast<uint8_t>(0xC0 | seq), 0x81, 0x00, 0x00},
                              Bytes(request.begin() + 2, request.end())});
    t.Send(echo);
    REQUIRE(queue.values.size() == 1);
    REQUIRE(queue.values.front().summary == TaskCompletion::SUCCESS);
}

TEST_CASE(SUITE("A challenge with no session keys fails the request, and sets the keys"))
{
    AuthFixture t;
    t.AddDefaultUser();
    t.context->OnLowerLayerUp();
    t.exe->run_many();
    // the outstation doesn't take the keys
    t.SetSessionKeys({0xAA, 0xBB, 0xCC, 0xDD}, false);

    CommandCallbackQueue queue;
    t.context->DirectOperate(CommandSet({WithIndex(ControlRelayOutputBlock(OperationType::PULSE_ON), 1)}),
                             queue.Callback(), TaskConfig::Default());
    t.exe->run_many();
    const auto request = t.PopWrite();
    REQUIRE(request[1] == 0x05);
    const auto seq = static_cast<uint8_t>(request[0] & 0x0F);

    t.Send(AuthResponse(seq, 1, Concat({UInt32(7), UInt16(1), {0x04, 0x01}, {0x10, 0x20, 0x30, 0x40}})));

    REQUIRE(queue.values.size() == 1);
    REQUIRE(queue.values.front().summary == TaskCompletion::FAILURE_BAD_AUTHENTICATION);

    // the master asks for the key status again
    REQUIRE(ToHex(Bytes(t.PopWrite())).substr(2) == " 20 78 04 07 01 01 00");
}

TEST_CASE(SUITE("A challenge with another sequence number is ignored"))
{
    AuthFixture t;
    t.AddDefaultUser();
    t.context->OnLowerLayerUp();
    t.exe->run_many();
    t.SetSessionKeys();

    CommandCallbackQueue queue;
    t.context->DirectOperate(CommandSet({WithIndex(ControlRelayOutputBlock(OperationType::PULSE_ON), 1)}),
                             queue.Callback(), TaskConfig::Default());
    t.exe->run_many();
    const auto request = t.PopWrite();
    const auto seq = static_cast<uint8_t>(request[0] & 0x0F);

    t.Send(AuthResponse((seq + 1) & 0x0F, 1, Concat({UInt32(7), UInt16(1), {0x04, 0x01}, {0x10, 0x20, 0x30, 0x40}})));
    REQUIRE(t.lower->NumWrites() == 0);
    REQUIRE(queue.values.empty());
}

TEST_CASE(SUITE("An authentication error fails the request"))
{
    AuthFixture t;
    t.AddDefaultUser();
    t.context->OnLowerLayerUp();
    t.exe->run_many();
    t.SetSessionKeys();

    CommandCallbackQueue queue;
    t.context->DirectOperate(CommandSet({WithIndex(ControlRelayOutputBlock(OperationType::PULSE_ON), 1)}),
                             queue.Callback(), TaskConfig::Default());
    t.exe->run_many();
    const auto request = t.PopWrite();
    const auto seq = static_cast<uint8_t>(request[0] & 0x0F);

    // g120v7: CSQ, user, association, error code 7 (authorization failed), a 6-byte time, no text
    const auto error = Concat({UInt32(7), UInt16(1), UInt16(0), {0x07}, Bytes(6, 0x00)});
    t.Send(AuthResponse(seq, 7, error));

    REQUIRE(queue.values.size() == 1);
    REQUIRE(queue.values.front().summary == TaskCompletion::FAILURE_NOT_AUTHORIZED);
}

TEST_CASE(SUITE("A user status change is sent as the authority made it, and its challenge answered"))
{
    AuthFixture t;
    t.AddDefaultUser();
    t.context->OnLowerLayerUp();
    t.exe->run_many();
    const auto controlKey = t.SetSessionKeys();

    t.context->ChangeUserStatus(UserStatusChange(KeyChangeMethod::AES_128_SHA1_HMAC, UserOperation::OP_ADD, 9, 2, 30,
                                                 "bob", {}, {0xC1, 0xC2, 0xC3}),
                                TaskConfig::Default());
    t.exe->run_many();
    const auto request = t.PopWrite();
    const auto seq = static_cast<uint8_t>(request[0] & 0x0F);

    // the method, the operation, SCS, role and expiry, then the three lengths, then the name, key and certification
    const auto object = Concat({{0x03, 0x01}, UInt32(9), UInt16(2), UInt16(30), UInt16(3), UInt16(0), UInt16(3),
                                {'b', 'o', 'b'}, {0xC1, 0xC2, 0xC3}});
    REQUIRE(ToHex(request)
            == ToHex(Concat({{static_cast<uint8_t>(0xC0 | seq), 0x20, 0x78, 0x0A, 0x5B, 0x01},
                             UInt16(static_cast<uint16_t>(object.size())), object})));

    const auto challenge = AuthResponse(seq, 1, Concat({UInt32(8), UInt16(1), {0x04, 0x01}, {0x10, 0x20, 0x30, 0x40}}));
    t.Send(challenge);
    const auto reply = t.PopWrite();
    REQUIRE(Bytes(reply.end() - 16, reply.end()) == HMAC256(controlKey, Concat({challenge, request}), 16));

    t.Send({static_cast<uint8_t>(0xC0 | seq), 0x81, 0x00, 0x00});
    REQUIRE(t.application->completions.back().type == MasterTaskType::USER_TASK);
    REQUIRE(t.application->completions.back().result == TaskCompletion::SUCCESS);
}

TEST_CASE(SUITE("Answering a challenge leaves no response timer of the challenged request running"))
{
    AuthFixture t;
    t.AddDefaultUser();
    t.context->OnLowerLayerUp();
    t.exe->run_many();
    t.SetSessionKeys();
    const auto timeout = NoStartupTasks().responseTimeout.value;

    CommandCallbackQueue first;
    t.context->DirectOperate(CommandSet({WithIndex(ControlRelayOutputBlock(OperationType::PULSE_ON), 1)}),
                             first.Callback(), TaskConfig::Default());
    t.exe->run_many();
    const auto request = t.PopWrite();
    const auto seq = static_cast<uint8_t>(request[0] & 0x0F);

    // the challenge comes halfway through the response timeout, and the control succeeds
    t.exe->advance_time(timeout / 2);
    t.exe->run_many();
    t.Send(AuthResponse(seq, 1, Concat({UInt32(7), UInt16(1), {0x04, 0x01}, {0x10, 0x20, 0x30, 0x40}})));
    t.PopWrite();
    t.Send(Concat({{static_cast<uint8_t>(0xC0 | seq), 0x81, 0x00, 0x00}, Bytes(request.begin() + 2, request.end())}));
    REQUIRE(first.values.front().summary == TaskCompletion::SUCCESS);

    // the next control is not timed out when the first request's timeout comes round
    CommandCallbackQueue second;
    t.context->DirectOperate(CommandSet({WithIndex(ControlRelayOutputBlock(OperationType::PULSE_ON), 1)}),
                             second.Callback(), TaskConfig::Default());
    t.exe->run_many();
    t.PopWrite();
    t.exe->advance_time(timeout / 2 + std::chrono::milliseconds(1));
    t.exe->run_many();
    REQUIRE(second.values.empty());
}

TEST_CASE(SUITE("A challenge for a user the master doesn't have fails the request with no user"))
{
    AuthFixture t;
    t.AddDefaultUser();
    t.context->OnLowerLayerUp();
    t.exe->run_many();
    t.SetSessionKeys();

    CommandCallbackQueue queue;
    t.context->DirectOperate(CommandSet({WithIndex(ControlRelayOutputBlock(OperationType::PULSE_ON), 1)}),
                             queue.Callback(), TaskConfig(TaskId::Undefined(), nullptr, User(5)));
    t.exe->run_many();
    const auto request = t.PopWrite();
    const auto seq = static_cast<uint8_t>(request[0] & 0x0F);

    t.Send(AuthResponse(seq, 1, Concat({UInt32(7), UInt16(5), {0x04, 0x01}, {0x10, 0x20, 0x30, 0x40}})));
    REQUIRE(queue.values.size() == 1);
    REQUIRE(queue.values.front().summary == TaskCompletion::FAILURE_NO_USER);

    // and it asks for no session keys for that user
    t.exe->advance_time(NoStartupTasks().taskRetryPeriod.value);
    t.exe->run_many();
    REQUIRE(t.lower->NumWrites() == 0);
}

TEST_CASE(SUITE("A user status change queued as the master comes online waits for the session keys"))
{
    AuthFixture t;
    t.AddDefaultUser();
    t.context->OnLowerLayerUp();
    t.context->ChangeUserStatus(UserStatusChange(KeyChangeMethod::AES_128_SHA1_HMAC, UserOperation::OP_ADD, 9, 2, 30,
                                                 "bob", {}, {0xC1, 0xC2, 0xC3}),
                                TaskConfig::Default());
    t.exe->run_many();

    // the key status request goes first
    t.SetSessionKeys();
    t.exe->run_many();
    const auto request = t.PopWrite();
    REQUIRE(ToHex(Bytes(request.begin() + 1, request.begin() + 4)) == "20 78 0A");
}
