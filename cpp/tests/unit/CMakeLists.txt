set(unittests_headers    

    ./utils/APDUHelpers.h
    ./utils/APDUHexBuilders.h
    ./utils/BufferHelpers.h
    ./utils/CallbackQueue.h
    ./utils/CommandCallbackQueue.h
    ./utils/CopyableBuffer.h	
    ./utils/DNPHelpers.h
    ./utils/LinkHex.h
    ./utils/LinkLayerTest.h
    ./utils/LinkParserTest.h
    ./utils/MasterTestFixture.h
    ./utils/MeasurementComparisons.h
    ./utils/MockTransportSegment.h
    ./utils/OutstationTestObject.h
    ./utils/ProtocolUtil.h
    ./utils/TransportTestObject.h
)

set(unittests_src
    ./main.cpp

    ./TestAPDUParsing.cpp
    ./TestAPDUWriting.cpp    
    ./TestCollectionTransform.cpp
    ./TestControlRelayOutputBlock.cpp
    ./TestDataSets.cpp
    ./TestDeviceAttrUnicode.cpp
    ./TestCRC.cpp
    ./TestEventStorage.cpp
    ./TestFlags.cpp
    ./TestGroup120.cpp
    ./TestIPEndpointsList.cpp
    ./TestLinkAddresses.cpp
    ./TestLinkFrame.cpp
    ./TestLinkLayer.cpp
    ./TestLinkLayerKeepAlive.cpp
    ./TestLinkReceiver.cpp
    ./TestList.cpp
    ./TestLog.cpp
    ./TestMaster.cpp
    ./TestMasterAssignClass.cpp
    ./TestMasterCommandRequests.cpp
    ./TestChallengeState.cpp
    ./TestSessionKeyChangeState.cpp
    ./TestMasterAuth.cpp
    ./TestEncryptedUpdateKey.cpp
    ./TestEventTriggers.cpp
    ./TestFloatSerialization.cpp
    ./TestKeyWrap.cpp
    ./TestTimestamp.cpp
    ./TestMasterFileTransfer.cpp
    ./TestOutstationFileTransfer.cpp
    ./TestMasterMultiCommandRequests.cpp
    ./TestMasterMultidrop.cpp
    ./TestMasterUnsolBehaviors.cpp
    ./TestMeasurementHandler.cpp
    ./TestOutstation.cpp
    ./TestOutstationBroadcast.cpp
    ./TestOutstationAssignClass.cpp
    ./TestOutstationCommandResponses.cpp
    ./TestOutstationDiscontiguousIndices.cpp
    ./TestOutstationEventResponses.cpp
    ./TestOutstationFrozenCounters.cpp
    ./TestOutstationStateMachine.cpp
    ./TestOutstationUnsolicitedResponses.cpp
    ./TestPcapParsing.cpp
    ./TestShiftableBuffer.cpp
	./TestStaticDataMap.cpp
	./TestDatabase.cpp
    ./TestTimeDuration.cpp
    ./TestTransportLayer.cpp
    ./TestTypedCommandHeader.cpp
    ./TestUpdateBuilder.cpp
    ./TestVirtualTerminal.cpp
    ./TestWriteConversions.cpp

    ./TestSecureCompare.cpp
    ./TestCriticalFunctions.cpp
    ./TestPermissions.cpp
    ./TestSessionKeys.cpp
    ./TestUpdateKey.cpp
    ./TestStatistics.cpp
    ./TestStatThresholds.cpp
    ./TestOutstationUserDatabase.cpp
    ./TestAggressiveModeParser.cpp
    ./TestAuthRequestParser.cpp
    ./TestDeferredASDU.cpp
    ./TestKeyChangeConfirmationHMAC.cpp

    ./utils/APDUHelpers.cpp
    ./utils/APDUHexBuilders.cpp
    ./utils/BufferHelpers.cpp
    ./utils/CopyableBuffer.cpp	
    ./utils/DNPHelpers.cpp
    ./utils/LinkHex.cpp
    ./utils/LinkLayerTest.cpp
    ./utils/MasterTestFixture.cpp
    ./utils/MockTransportSegment.cpp
    ./utils/OutstationTestObject.cpp
    ./utils/ProtocolUtil.cpp
    ./utils/TransportTestObject.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${unittests_headers} ${unittests_src}
)

add_executable(unittests
    ${unittests_headers} ${unittests_src}
)
target_compile_features(unittests PRIVATE cxx_std_14)
# OpenSSL directly: the Secure Authentication tests check the master with it
target_link_libraries(unittests PRIVATE catch dnp3mocks OpenSSL::Crypto)
target_include_directories(unittests PRIVATE ./ ../../lib/src)
set_target_properties(unittests PROPERTIES FOLDER cpp/tests)
add_test(NAME unittests COMMAND unittests)

clang_format(unittests)
clang_tidy(unittests)
