/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */
#pragma once

#include <lib/support/TestPersistentStorageDelegate.h>
#include <messaging/ExchangeContext.h>
#include <messaging/ExchangeMgr.h>
#include <protocols/secure_channel/MessageCounterManager.h>
#include <protocols/secure_channel/PASESession.h>
#include <system/SystemClock.h>
#include <transport/SessionManager.h>
#include <transport/TransportMgr.h>
#include <transport/tests/LoopbackTransportManager.h>

#include <nlunit-test.h>

namespace chip {
namespace Test {

/**
 * @brief
 *  Test contexts that use Platform::Memory and might call Free() on destruction can inherit from this class and call its Init().
 *  Platform::MemoryShutdown() will then be called after the subclasses' destructor.
 */
class PlatformMemoryUser
{
public:
    PlatformMemoryUser() : mInitialized(false) {}
    ~PlatformMemoryUser()
    {
        if (mInitialized)
        {
            chip::Platform::MemoryShutdown();
        }
    }
    CHIP_ERROR Init()
    {
        CHIP_ERROR status = CHIP_NO_ERROR;
        if (!mInitialized)
        {
            status       = chip::Platform::MemoryInit();
            mInitialized = (status == CHIP_NO_ERROR);
        }
        return status;
    }

private:
    bool mInitialized;
};

/**
 * @brief The context of test cases for messaging layer. It wil initialize network layer and system layer, and create
 *        two secure sessions, connected with each other. Exchanges can be created for each secure session.
 */
class MessagingContext : public PlatformMemoryUser
{
public:
    MessagingContext() :
        mInitialized(false), mAliceAddress(Transport::PeerAddress::UDP(GetAddress(), CHIP_PORT + 1)),
        mBobAddress(Transport::PeerAddress::UDP(GetAddress(), CHIP_PORT))
    {}
    ~MessagingContext() { VerifyOrDie(mInitialized == false); }

    // Whether Alice and Bob are initialized, must be called before Init
    void ConfigInitializeNodes(bool initializeNodes) { mInitializeNodes = initializeNodes; }

    /// Initialize the underlying layers and test suite pointer
    CHIP_ERROR Init(TransportMgrBase * transport, IOContext * io);

    // Shutdown all layers, finalize operations
    CHIP_ERROR Shutdown();

    // Initialize from an existing messaging context.  Useful if we want to
    // share some state (like the transport).
    CHIP_ERROR InitFromExisting(const MessagingContext & existing);

    // The shutdown method to use if using InitFromExisting.  Must pass in the
    // same existing context as was passed to InitFromExisting.
    CHIP_ERROR ShutdownAndRestoreExisting(MessagingContext & existing);

    static Inet::IPAddress GetAddress()
    {
        Inet::IPAddress addr;
        Inet::IPAddress::FromString("::1", addr);
        return addr;
    }

    static const uint16_t kBobKeyId   = 1;
    static const uint16_t kAliceKeyId = 2;
    NodeId GetBobNodeId() const;
    NodeId GetAliceNodeId() const;
    GroupId GetFriendsGroupId() const { return mFriendsGroupId; }

    SessionManager & GetSecureSessionManager() { return mSessionManager; }
    Messaging::ExchangeManager & GetExchangeManager() { return mExchangeManager; }
    secure_channel::MessageCounterManager & GetMessageCounterManager() { return mMessageCounterManager; }

    FabricIndex GetAliceFabricIndex() { return mAliceFabricIndex; }
    FabricIndex GetBobFabricIndex() { return mBobFabricIndex; }
    FabricInfo * GetAliceFabric() { return mFabricTable.FindFabricWithIndex(mAliceFabricIndex); }
    FabricInfo * GetBobFabric() { return mFabricTable.FindFabricWithIndex(mBobFabricIndex); }

    CHIP_ERROR CreateSessionBobToAlice();
    CHIP_ERROR CreateSessionAliceToBob();
    CHIP_ERROR CreateSessionBobToFriends();

    void ExpireSessionBobToAlice();
    void ExpireSessionAliceToBob();
    void ExpireSessionBobToFriends();

    SessionHandle GetSessionBobToAlice();
    SessionHandle GetSessionAliceToBob();
    SessionHandle GetSessionBobToFriends();

    const Transport::PeerAddress & GetAliceAddress() { return mAliceAddress; }
    const Transport::PeerAddress & GetBobAddress() { return mBobAddress; }

    Messaging::ExchangeContext * NewUnauthenticatedExchangeToAlice(Messaging::ExchangeDelegate * delegate);
    Messaging::ExchangeContext * NewUnauthenticatedExchangeToBob(Messaging::ExchangeDelegate * delegate);

    Messaging::ExchangeContext * NewExchangeToAlice(Messaging::ExchangeDelegate * delegate);
    Messaging::ExchangeContext * NewExchangeToBob(Messaging::ExchangeDelegate * delegate);

    System::Layer & GetSystemLayer() { return mIOContext->GetSystemLayer(); }

private:
    bool mInitializeNodes = true;
    bool mInitialized;
    FabricTable mFabricTable;
    SessionManager mSessionManager;
    Messaging::ExchangeManager mExchangeManager;
    secure_channel::MessageCounterManager mMessageCounterManager;
    IOContext * mIOContext;
    TransportMgrBase * mTransport;                // Only needed for InitFromExisting.
    chip::TestPersistentStorageDelegate mStorage; // for SessionManagerInit

    FabricIndex mAliceFabricIndex = kUndefinedFabricIndex;
    FabricIndex mBobFabricIndex   = kUndefinedFabricIndex;
    GroupId mFriendsGroupId       = 0x0101;
    Transport::PeerAddress mAliceAddress;
    Transport::PeerAddress mBobAddress;
    SessionHolder mSessionAliceToBob;
    SessionHolder mSessionBobToAlice;
    Optional<Transport::OutgoingGroupSession> mSessionBobToFriends;
};

// LoopbackMessagingContext enriches MessagingContext with an async loopback transport
class LoopbackMessagingContext : public LoopbackTransportManager, public MessagingContext
{
public:
    virtual ~LoopbackMessagingContext() {}

    /// Initialize the underlying layers.
    virtual CHIP_ERROR Init()
    {
        ReturnErrorOnFailure(chip::Platform::MemoryInit());
        ReturnErrorOnFailure(LoopbackTransportManager::Init());
        ReturnErrorOnFailure(MessagingContext::Init(&GetTransportMgr(), &GetIOContext()));
        return CHIP_NO_ERROR;
    }

    // Shutdown all layers, finalize operations
    virtual CHIP_ERROR Shutdown()
    {
        ReturnErrorOnFailure(MessagingContext::Shutdown());
        ReturnErrorOnFailure(LoopbackTransportManager::Shutdown());
        chip::Platform::MemoryShutdown();
        return CHIP_NO_ERROR;
    }

    // Init/Shutdown Helpers that can be used directly as the nlTestSuite
    // initialize/finalize function.
    static int Initialize(void * context)
    {
        auto * ctx = static_cast<LoopbackMessagingContext *>(context);
        return ctx->Init() == CHIP_NO_ERROR ? SUCCESS : FAILURE;
    }

    static int Finalize(void * context)
    {
        auto * ctx = static_cast<LoopbackMessagingContext *>(context);
        return ctx->Shutdown() == CHIP_NO_ERROR ? SUCCESS : FAILURE;
    }

    using LoopbackTransportManager::GetSystemLayer;
};

} // namespace Test
} // namespace chip
