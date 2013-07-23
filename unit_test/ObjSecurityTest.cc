/******************************************************************************
 * Copyright 2013-2014, Qualcomm Innovation Center, Inc.
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
 ******************************************************************************/

#include <gtest/gtest.h>
#include "ajTestCommon.h"
#include <alljoyn/Message.h>
#include <alljoyn/BusAttachment.h>
#include <alljoyn/BusObject.h>
#include <alljoyn/ProxyBusObject.h>
#include <alljoyn/InterfaceDescription.h>
#include <alljoyn/DBusStd.h>
#include <qcc/Thread.h>
#include <qcc/Util.h>

using namespace ajn;
using namespace qcc;

const char* interface1 = "org.alljoyn.alljoyn_test.interface1";
const char* interface2 = "org.alljoyn.alljoyn_test.interface2";
const char* object_path = "/org/alljoyn/alljoyn_test";

class SvcTestObject : public BusObject {

  public:

    SvcTestObject(const char* path, BusAttachment& mBus) :
        BusObject(path),
        msgEncrypted(false),
        objectRegistered(false),
        get_property_called(false),
        set_property_called(false),
        prop_val(420),
        bus(mBus)
    {
        QStatus status = ER_OK;
        //const BusAttachment &mBus = GetBusAttachment();
        /* Add interface1 to the BusObject. */
        const InterfaceDescription* Intf1 = mBus.GetInterface(interface1);
        EXPECT_TRUE(Intf1 != NULL);
        AddInterface(*Intf1);
        /* Add interface2 to the BusObject. */
        const InterfaceDescription* Intf2 = mBus.GetInterface(interface2);
        EXPECT_TRUE(Intf2 != NULL);
        AddInterface(*Intf2);

        /* Register the method handlers with the object */
        const MethodEntry methodEntries[] = {
            { Intf1->GetMember("my_ping"), static_cast<MessageReceiver::MethodHandler>(&SvcTestObject::Ping) },
        };
        status = AddMethodHandlers(methodEntries, ArraySize(methodEntries));
        EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    }

    void ObjectRegistered(void)
    {
        //const String introspect = GenerateIntrospection(true);
        //printf("-------------------------------------------------------\n");
        //printf("\n Introspection of PestObject data is \n ------------------------------------------------------\n %s \n", introspect.c_str());
        objectRegistered = true;
    }

    void Ping(const InterfaceDescription::Member* member, Message& msg)
    {
        char* value = NULL;
        const MsgArg* arg((msg->GetArg(0)));
        QStatus status = arg->Get("s", &value);
        EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
        if (msg->IsEncrypted()) {
            msgEncrypted = true;
        }
        status = MethodReply(msg, arg, 1);
        EXPECT_EQ(ER_OK, status) << "Ping: Error sending reply,  Actual Status: " << QCC_StatusText(status);
    }

    void GetProp(const InterfaceDescription::Member* member, Message& msg)
    {
        QStatus status = ER_OK;
        const InterfaceDescription* Intf2 = bus.GetInterface(interface2);
        EXPECT_TRUE(Intf2 != NULL);

        if (!msg->IsEncrypted() &&  (this->IsSecure() && Intf2->GetSecurityPolicy() != AJ_IFC_SECURITY_OFF)) {
            status = ER_BUS_MESSAGE_NOT_ENCRYPTED;
            status = MethodReply(msg, status);
            EXPECT_EQ(ER_OK, status) << "Actual Status: " << QCC_StatusText(status);
        } else {
            get_property_called = true;
            MsgArg prop("v", new MsgArg("i", prop_val));
            if (msg->IsEncrypted()) {
                msgEncrypted = true;
            }
            status = MethodReply(msg, &prop, 1);
            EXPECT_EQ(ER_OK, status) << "Error getting property, Actual Status: " << QCC_StatusText(status);
        }
    }

    void SetProp(const InterfaceDescription::Member* member, Message& msg)
    {
        QStatus status = ER_OK;
        const InterfaceDescription* Intf2 = bus.GetInterface(interface2);
        EXPECT_TRUE(Intf2 != NULL);

        if (!msg->IsEncrypted() &&  (this->IsSecure() && Intf2->GetSecurityPolicy() != AJ_IFC_SECURITY_OFF)) {
            status = ER_BUS_MESSAGE_NOT_ENCRYPTED;
            status = MethodReply(msg, status);
            EXPECT_EQ(ER_OK, status) << "Actual Status: " << QCC_StatusText(status);
        } else {
            set_property_called = true;
            int32_t integer = 0;
            if (msg->IsEncrypted()) {
                msgEncrypted = true;
            }
            const MsgArg* val = msg->GetArg(2);
            MsgArg value = *(val->v_variant.val);
            status = value.Get("i", &integer);
            EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
            prop_val = integer;
            status = MethodReply(msg, status);
            EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
        }
    }


    bool msgEncrypted;
    bool objectRegistered;
    bool get_property_called;
    bool set_property_called;
    int32_t prop_val;
    BusAttachment& bus;

};


class ObjectSecurityTest : public testing::Test, public AuthListener {
  public:
    ObjectSecurityTest() :
        clientbus("ProxyBusObjectTestClient", false),
        servicebus("ProxyBusObjectTestService", false),
        status(ER_OK),
        authComplete(false)
    { };

    virtual void SetUp() {
        status = clientbus.Start();
        EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
        status = clientbus.Connect(ajn::getConnectArg().c_str());
        EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
        clientbus.EnablePeerSecurity("ALLJOYN_SRP_KEYX", this, NULL, false);
        clientbus.ClearKeyStore();

        status = servicebus.Start();
        EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
        status = servicebus.Connect(ajn::getConnectArg().c_str());
        EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
        servicebus.EnablePeerSecurity("ALLJOYN_SRP_KEYX", this, NULL, false);
        servicebus.ClearKeyStore();
    }

    virtual void TearDown() {

        status = clientbus.Disconnect(ajn::getConnectArg().c_str());
        EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
        status = servicebus.Disconnect(ajn::getConnectArg().c_str());
        EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
        status = clientbus.Stop();
        EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
        status = servicebus.Stop();
        EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
        status = clientbus.Join();
        EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
        status = servicebus.Join();
        EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    }

    BusAttachment clientbus;
    BusAttachment servicebus;
    QStatus status;
    bool authComplete;

  private:

    bool RequestCredentials(const char* authMechanism, const char* authPeer, uint16_t authCount, const char* userId, uint16_t credMask, Credentials& creds) {
        EXPECT_STREQ("ALLJOYN_SRP_KEYX", authMechanism);
        if (credMask & AuthListener::CRED_PASSWORD) {
            creds.SetPassword("123456");
        }
        return true;
    }

    void AuthenticationComplete(const char* authMechanism, const char* authPeer, bool success) {
        EXPECT_STREQ("ALLJOYN_SRP_KEYX", authMechanism);
        EXPECT_TRUE(success);
        authComplete = true;
    }

};

/*
 *  Service object level = false
 *  Client object level = false
 *  service creates interface with OFF.
 *  client creates interface with OFF.
 *  client makes method call.
 *  expected that no encryption is used.
 */
TEST_F(ObjectSecurityTest, Test1) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    status = servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);

    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    InterfaceDescription* clienttestIntf2 = NULL;
    status = clientbus.CreateInterface(interface2, clienttestIntf2, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf2->Activate();


    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    status = clientProxyObject.AddInterface(interface1);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    status = clientProxyObject.AddInterface(interface2);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    ASSERT_FALSE(serviceObject.IsSecure());
    ASSERT_FALSE(clientProxyObject.IsSecure());
}


/*
 *  Service object level = false
 *  Client object level = false
 *  service creates interface with OFF.
 *  client Introspects.
 *  client makes method call.
 *  expected that no encryption is used.
 */
TEST_F(ObjectSecurityTest, Test2) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    status = clientProxyObject.IntrospectRemoteObject();

    serviceObject.msgEncrypted = false;
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    ASSERT_FALSE(serviceObject.IsSecure());
    ASSERT_FALSE(clientProxyObject.IsSecure());
}


/*
 *   Service object level = false
 *   Client object level = false
 *   service creates interface with INHERIT.
 *   client creates interface with INHERIT.
 *   client makes method call.
 *   expected that no encryption is used.
 */
TEST_F(ObjectSecurityTest, Test3) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    InterfaceDescription* clienttestIntf2 = NULL;
    status = clientbus.CreateInterface(interface2, clienttestIntf2, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf2->Activate();

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    status = clientProxyObject.AddInterface(interface1);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    status = clientProxyObject.AddInterface(interface2);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    ASSERT_FALSE(serviceObject.IsSecure());
    ASSERT_FALSE(clientProxyObject.IsSecure());
}

/*
 *   Service object level = false
 *   Client object level = false
 *   service creates interface with INHERIT.
 *   client Introspects.
 *   client makes method call.
 *   expected that no encryption is used.
 */
TEST_F(ObjectSecurityTest, Test4) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    status = clientProxyObject.IntrospectRemoteObject();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    ASSERT_FALSE(serviceObject.IsSecure());
    ASSERT_FALSE(clientProxyObject.IsSecure());
}

/*
 *  Service object level = false
 *  Client object level = false
 *  service creates interface with REQUIRED.
 *  client creates interface with REQUIRED.
 *  client makes method call.
 *  expected that encryption is used.
 */
TEST_F(ObjectSecurityTest, Test5) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    InterfaceDescription* clienttestIntf2 = NULL;
    status = clientbus.CreateInterface(interface2, clienttestIntf2, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf2->Activate();

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    status = clientProxyObject.AddInterface(interface1);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    status = clientProxyObject.AddInterface(interface2);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    ASSERT_FALSE(serviceObject.IsSecure());
    ASSERT_FALSE(clientProxyObject.IsSecure());
}



/*
 *  Service object level = false
 *  Client object level = false
 *  service creates interface with REQUIRED.
 *  client Introspects.
 *  client makes method call.
 *  expected that encryption is used.
 */
TEST_F(ObjectSecurityTest, Test6) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    status = clientProxyObject.IntrospectRemoteObject();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    ASSERT_FALSE(serviceObject.IsSecure());
    ASSERT_FALSE(clientProxyObject.IsSecure());
}

/*
 *  Service object level = false
 *  Client object level = true
 *   service creates interface with OFF.
 * client creates interface with OFF.
 *   client makes method call.
 *  expected that no encryption is used because interfaces with N/A security level should NOT use security.
 */

TEST_F(ObjectSecurityTest, DISABLED_Test7) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    InterfaceDescription* clienttestIntf2 = NULL;
    status = clientbus.CreateInterface(interface2, clienttestIntf2, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf2->Activate();

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, true);
    status = clientProxyObject.AddInterface(interface1);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    status = clientProxyObject.AddInterface(interface2);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    ASSERT_FALSE(serviceObject.IsSecure());
    ASSERT_TRUE(clientProxyObject.IsSecure());
}


/*
 *  Service object level = false
 *  Client object level = true
 *   service creates interface with OFF.
 *  client Introspects.
 *  client makes method call.
 *  expected that no encryption is used because interfaces with N/A security level should NOT use security.
 */
TEST_F(ObjectSecurityTest, DISABLED_Test8) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* clienttestIntf2 = NULL;
    status = servicebus.CreateInterface(interface2, clienttestIntf2, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, true);
    status = clientProxyObject.IntrospectRemoteObject();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    ASSERT_FALSE(serviceObject.IsSecure());
    ASSERT_TRUE(clientProxyObject.IsSecure());
}



/*
 *  Service object level = false
 *  Client object level = true
 *   service creates interface with INHERIT.
 *  client creates interface with INHERIT.
 *  client makes method call.
 * expected that encryption is used.
 */
TEST_F(ObjectSecurityTest, Test9) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    InterfaceDescription* clienttestIntf2 = NULL;
    status = clientbus.CreateInterface(interface2, clienttestIntf2, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf2->Activate();

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, true);
    status = clientProxyObject.AddInterface(interface1);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    status = clientProxyObject.AddInterface(interface2);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    ASSERT_FALSE(serviceObject.IsSecure());
    ASSERT_TRUE(clientProxyObject.IsSecure());
}

/*
 * Service object level = false
 * Client object level = true
 * service creates interface with INHERIT.
 * client Introspects.
 * client makes method call.
 * expected that encryption is used.
 */
TEST_F(ObjectSecurityTest, Test10) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, true);
    status = clientProxyObject.IntrospectRemoteObject();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    ASSERT_FALSE(serviceObject.IsSecure());
    ASSERT_TRUE(clientProxyObject.IsSecure());
}

/*
 * Service object level = false
 * Client object level = true
 * service creates interface with REQUIRED.
 * client creates interface with REQUIRED.
 * client makes method call.
 * expected that encryption is used.
 */
TEST_F(ObjectSecurityTest, Test11) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    InterfaceDescription* clienttestIntf2 = NULL;
    status = clientbus.CreateInterface(interface2, clienttestIntf2, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf2->Activate();

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, true);
    status = clientProxyObject.AddInterface(interface1);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    status = clientProxyObject.AddInterface(interface2);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    ASSERT_FALSE(serviceObject.IsSecure());
    ASSERT_TRUE(clientProxyObject.IsSecure());
}

/*
 * Service object level = false
 * Client object level = true
 * service creates interface with REQUIRED.
 * client Introspects.
 * client makes method call.
 * expected that encryption is used.
 */
TEST_F(ObjectSecurityTest, Test12) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, true);
    status = clientProxyObject.IntrospectRemoteObject();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    ASSERT_FALSE(serviceObject.IsSecure());
    ASSERT_TRUE(clientProxyObject.IsSecure());
}

/*
 * Service object level = true
 * Client object level = false
 * service creates interface with OFF.
 * client creates interface with OFF.
 * client makes method call.
 * expected that no encryption is used because interfaces with N/A security level should NOT use security.
 */
TEST_F(ObjectSecurityTest, DISABLED_Test13) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);

    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    InterfaceDescription* clienttestIntf2 = NULL;
    status = clientbus.CreateInterface(interface2, clienttestIntf2, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf2->Activate();

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    status = clientProxyObject.AddInterface(interface1);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    status = clientProxyObject.AddInterface(interface2);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    ASSERT_TRUE(serviceObject.IsSecure());
    ASSERT_FALSE(clientProxyObject.IsSecure());

}


/*
 * Service object level = true
 * Client object level = false
 * service creates interface with OFF.
 * client Introspects.
 * client makes method call.
 * expected that no encryption is used because interfaces with N/A security level should NOT use security.
 */
TEST_F(ObjectSecurityTest, DISABLED_Test14) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    /* Before introspect, proxybusobject is unsecure. */
    ASSERT_FALSE(clientProxyObject.IsSecure());

    status = clientProxyObject.IntrospectRemoteObject();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    ASSERT_TRUE(serviceObject.IsSecure());
    /* After introspect, proxybusobject becomes secure. */
    ASSERT_TRUE(clientProxyObject.IsSecure());

}



/*
 * Service object level = true
 * Client object level = false
 * service creates interface with INHERIT.
 * client creates interface with INHERIT.
 * client makes method call.
 * expected that calls will fail. This is because, client has no way of knowing that the object level security on service side is true.
 * Thus, service interface is INHERIT, it turns upto object level security.
 */
TEST_F(ObjectSecurityTest, Test15) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    InterfaceDescription* clienttestIntf2 = NULL;
    status = clientbus.CreateInterface(interface2, clienttestIntf2, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf2->Activate();

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    status = clientProxyObject.AddInterface(interface1);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    //Method call will fail
    EXPECT_EQ(ER_BUS_REPLY_IS_ERROR_MESSAGE, status) << "  Actual Status: " << QCC_StatusText(status);
    qcc::String errDescription;
    const char* errName = reply->GetErrorName(&errDescription);
    EXPECT_STREQ("org.alljoyn.Bus.SecurityViolation", errName);
    EXPECT_STREQ("Expected secure method call", errDescription.c_str());
    EXPECT_FALSE(serviceObject.msgEncrypted);

    status = clientProxyObject.AddInterface(interface2);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_BUS_REPLY_IS_ERROR_MESSAGE, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_FALSE(serviceObject.msgEncrypted);
    //Ensure set_property is not called
    EXPECT_FALSE(serviceObject.set_property_called);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_BUS_REPLY_IS_ERROR_MESSAGE, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_FALSE(serviceObject.msgEncrypted);
    //ensure get_property is not called
    EXPECT_FALSE(serviceObject.get_property_called);

    ASSERT_TRUE(serviceObject.IsSecure());
    ASSERT_FALSE(clientProxyObject.IsSecure());

}

/*
 *  Service object level = true
 *  Client object level = false
 *  service creates interface with INHERIT.
 *  client Introspects.
 *  client makes method call.
 *  expected that encryption is used.
 */
TEST_F(ObjectSecurityTest, Test16) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    /* Before introspect, proxybusobject is unsecure. */
    ASSERT_FALSE(clientProxyObject.IsSecure());

    status = clientProxyObject.IntrospectRemoteObject();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    ASSERT_TRUE(serviceObject.IsSecure());
    /* After introspect, proxybusobject becomes secure. */
    ASSERT_TRUE(clientProxyObject.IsSecure());

}

/*
 *  Service object level = true
 * Client object level = false
 *  service creates interface with REQUIRED.
 *  client creates interface with REQUIRED.
 *  client makes method call.
 *  expected that encryption is used.
 */
TEST_F(ObjectSecurityTest, Test17) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    InterfaceDescription* clienttestIntf2 = NULL;
    status = clientbus.CreateInterface(interface2, clienttestIntf2, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf2->Activate();

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    status = clientProxyObject.AddInterface(interface1);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    status = clientProxyObject.AddInterface(interface2);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    ASSERT_TRUE(serviceObject.IsSecure());
    ASSERT_FALSE(clientProxyObject.IsSecure());

}

/*
 *   Service object level = true
 *   Client object level = false
 *   service creates interface with REQUIRED.
 *   client Introspects.
 *   client makes method call.
 *   expected that encryption is used.
 */
TEST_F(ObjectSecurityTest, Test18) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    /* Before introspect, proxybusobject is unsecure. */
    ASSERT_FALSE(clientProxyObject.IsSecure());

    status = clientProxyObject.IntrospectRemoteObject();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    ASSERT_TRUE(serviceObject.IsSecure());
    /* After introspect, proxybusobject becomes secure. */
    ASSERT_TRUE(clientProxyObject.IsSecure());

}


/*
 *   Service object level = true
 *   Client object level = true
 *   service creates interface with OFF.
 *   client creates interface with OFF.
 *   client makes method call.
 *  expected that no encryption is used because interfaces with N/A security level should NOT use security.
 */
TEST_F(ObjectSecurityTest, DISABLED_Test19) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    InterfaceDescription* clienttestIntf2 = NULL;
    status = clientbus.CreateInterface(interface2, clienttestIntf2, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf2->Activate();

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, true);
    status = clientProxyObject.AddInterface(interface1);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    status = clientProxyObject.AddInterface(interface2);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    ASSERT_TRUE(serviceObject.IsSecure());
    ASSERT_TRUE(clientProxyObject.IsSecure());

}


/*
 *  Service object level = true
 *  Client object level = true
 *  service creates interface with OFF.
 *  client Introspects.
 *  client makes method call.
 *  expected that no encryption is used because interfaces with N/A security level should NOT use security.
 */
TEST_F(ObjectSecurityTest, DISABLED_Test20) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, true);
    status = clientProxyObject.IntrospectRemoteObject();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_FALSE(serviceObject.msgEncrypted);

    ASSERT_TRUE(serviceObject.IsSecure());
    ASSERT_TRUE(clientProxyObject.IsSecure());

}

/*
 *  Service object level = true
 *  Client object level = true
 *  service creates interface with INHERIT.
 *  client creates interface with INHERIT.
 *  client makes method call.
 *  expected that encryption is used.
 */
TEST_F(ObjectSecurityTest, Test21) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    InterfaceDescription* clienttestIntf2 = NULL;
    status = clientbus.CreateInterface(interface2, clienttestIntf2, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf2->Activate();

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, true);
    status = clientProxyObject.AddInterface(interface1);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    status = clientProxyObject.AddInterface(interface2);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    ASSERT_TRUE(serviceObject.IsSecure());
    ASSERT_TRUE(clientProxyObject.IsSecure());

}

/*
 *   Service object level = true
 *   Client object level = true
 *   service creates interface with INHERIT.
 *   client Introspects.
 *   client makes method call.
 *   expected that encryption is used.
 */
TEST_F(ObjectSecurityTest, Test22) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, true);
    status = clientProxyObject.IntrospectRemoteObject();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    ASSERT_TRUE(serviceObject.IsSecure());
    ASSERT_TRUE(clientProxyObject.IsSecure());

}

/*
 * Service object level = true
 * Client object level = true
 * service creates interface with REQUIRED.
 * client creates interface with REQUIRED.
 * client makes method call.
 * expected that encryption is used.
 */
TEST_F(ObjectSecurityTest, Test23) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    InterfaceDescription* clienttestIntf2 = NULL;
    status = clientbus.CreateInterface(interface2, clienttestIntf2, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf2->Activate();

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, true);
    status = clientProxyObject.AddInterface(interface1);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    status = clientProxyObject.AddInterface(interface2);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    ASSERT_TRUE(serviceObject.IsSecure());
    ASSERT_TRUE(clientProxyObject.IsSecure());

}

/*
 *  Service object level = true
 *  Client object level = true
 *  service creates interface with REQUIRED.
 *  client Introspects.
 *  client makes method call.
 *  expected that encryption is used.
 */
TEST_F(ObjectSecurityTest, Test24) {

    QStatus status = ER_OK;

    InterfaceDescription* Intf1 = NULL;
    status = servicebus.CreateInterface(interface1, Intf1, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf1->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf1->Activate();
    InterfaceDescription* Intf2 = NULL;
    status = servicebus.CreateInterface(interface2, Intf2, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = Intf2->AddProperty("integer_property", "i", PROP_ACCESS_RW);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Intf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, true);
    status = clientProxyObject.IntrospectRemoteObject();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    Message reply(clientbus);
    const InterfaceDescription::Member* pingMethod;
    const InterfaceDescription* ifc = clientProxyObject.GetInterface(interface1);
    pingMethod = ifc->GetMember("my_ping");
    MsgArg pingArgs;
    status = pingArgs.Set("s", "Ping String");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.MethodCall(*pingMethod, &pingArgs, 1, reply, 5000);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_STREQ("Ping String", reply->GetArg(0)->v_string.str);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    MsgArg val;
    status = val.Set("i", 421);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clientProxyObject.SetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    serviceObject.msgEncrypted = false;
    status = clientProxyObject.GetProperty(interface2, "integer_property", val);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    int iVal = 0;
    status = val.Get("i", &iVal);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_EQ(421, iVal);
    EXPECT_TRUE(serviceObject.msgEncrypted);

    ASSERT_TRUE(serviceObject.IsSecure());
    ASSERT_TRUE(clientProxyObject.IsSecure());

}

/*
 *  Client object level = true.
 *  service creates interface with REQUIRED.
 *  Client Introspect should not trigger security.
 */
TEST_F(ObjectSecurityTest, Test25) {

    QStatus status = ER_OK;

    InterfaceDescription* servicetestIntf = NULL;
    status = servicebus.CreateInterface(interface1, servicetestIntf, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = servicetestIntf->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    servicetestIntf->Activate();
    InterfaceDescription* servicetestIntf2 = NULL;
    status = servicebus.CreateInterface(interface2, servicetestIntf2, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = servicetestIntf2->AddMethod("my_ping", "s", "s", "inStr,outStr", 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    servicetestIntf2->Activate();

    SvcTestObject serviceObject(object_path, servicebus);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(serviceObject.objectRegistered);


    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, true);
    status = clientProxyObject.IntrospectRemoteObject();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_FALSE(authComplete);
}



class SignalSecurityTestObject : public BusObject {

  public:

    SignalSecurityTestObject(const char* path, InterfaceDescription& intf) :
        BusObject(path),
        objectRegistered(false),
        intf(intf)  { }

    void ObjectRegistered(void)
    {
        objectRegistered = true;
    }

    QStatus SendSignal() {
        const InterfaceDescription::Member*  signal_member = intf.GetMember("my_signal");
        MsgArg arg("s", "Signal");
        QStatus status = Signal(NULL, 0, *signal_member, &arg, 1, 0, 0);
        return status;
    }


    bool objectRegistered;
    InterfaceDescription& intf;
};

class SignalReceiver : public MessageReceiver {

  public:

    SignalReceiver() {
        signalReceived = false;
        msgEncrypted = false;
    }

    void SignalHandler(const InterfaceDescription::Member* member, const char* sourcePath, Message& msg) {
        signalReceived = true;
        if (msg->IsEncrypted()) {
            msgEncrypted = true;;
        }
    }

    bool signalReceived;
    bool msgEncrypted;

};

/*
 *  signal sender object level = false.
 *  service creates interface with OFF.
 *  Signal is not encrypted.
 */

TEST_F(ObjectSecurityTest, DISABLED_Test26) {

    QStatus status = ER_OK;

    InterfaceDescription* servicetestIntf = NULL;
    status = servicebus.CreateInterface(interface1, servicetestIntf, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = servicetestIntf->AddSignal("my_signal", "s", NULL, 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    servicetestIntf->Activate();

    SignalSecurityTestObject serviceObject(object_path, *servicetestIntf);
    servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddSignal("my_signal", "s", NULL, 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    const InterfaceDescription::Member*  signal_member = clienttestIntf->GetMember("my_signal");

    SignalReceiver signalReceiver;
    status = clientbus.RegisterSignalHandler(&signalReceiver,
                                             static_cast<MessageReceiver::SignalHandler>(&SignalReceiver::SignalHandler),
                                             signal_member,
                                             NULL);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    status = clientbus.AddMatch("type='signal',interface='org.alljoyn.alljoyn_test.interface1',member='my_signal'");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    EXPECT_FALSE(clientProxyObject.IsSecure());
    status = clientProxyObject.SecureConnection();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(clientProxyObject.IsSecure());

    status = serviceObject.SendSignal();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    //Wait for a maximum of 2 sec for signal to be arrived
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(signalReceiver.signalReceived);
    ASSERT_FALSE(signalReceiver.msgEncrypted);

}


/*
 *  signal sender object level = false.
 *  service creates interface with INHERIT.
 *  Signal is not encrypted.
 */

TEST_F(ObjectSecurityTest, DISABLED_Test27) {

    QStatus status = ER_OK;

    InterfaceDescription* servicetestIntf = NULL;
    status = servicebus.CreateInterface(interface1, servicetestIntf, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = servicetestIntf->AddSignal("my_signal", "s", NULL, 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    servicetestIntf->Activate();

    SignalSecurityTestObject serviceObject(object_path, *servicetestIntf);
    servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddSignal("my_signal", "s", NULL, 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    const InterfaceDescription::Member*  signal_member = clienttestIntf->GetMember("my_signal");

    SignalReceiver signalReceiver;
    status = clientbus.RegisterSignalHandler(&signalReceiver,
                                             static_cast<MessageReceiver::SignalHandler>(&SignalReceiver::SignalHandler),
                                             signal_member,
                                             NULL);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    status = clientbus.AddMatch("type='signal',interface='org.alljoyn.alljoyn_test.interface1',member='my_signal'");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    EXPECT_FALSE(clientProxyObject.IsSecure());
    status = clientProxyObject.SecureConnection();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(clientProxyObject.IsSecure());

    status = serviceObject.SendSignal();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    //Wait for a maximum of 2 sec for signal to be arrived
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(signalReceiver.signalReceived);
    ASSERT_FALSE(signalReceiver.msgEncrypted);

}


/*
 *  signal sender object level = false.
 *  service creates interface with REQUIRED.
 *  Signal is  encrypted.
 */

TEST_F(ObjectSecurityTest, DISABLED_Test28) {

    QStatus status = ER_OK;

    InterfaceDescription* servicetestIntf = NULL;
    status = servicebus.CreateInterface(interface1, servicetestIntf, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = servicetestIntf->AddSignal("my_signal", "s", NULL, 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    servicetestIntf->Activate();

    SignalSecurityTestObject serviceObject(object_path, *servicetestIntf);
    servicebus.RegisterBusObject(serviceObject, false);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddSignal("my_signal", "s", NULL, 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    const InterfaceDescription::Member*  signal_member = clienttestIntf->GetMember("my_signal");

    SignalReceiver signalReceiver;
    status = clientbus.RegisterSignalHandler(&signalReceiver,
                                             static_cast<MessageReceiver::SignalHandler>(&SignalReceiver::SignalHandler),
                                             signal_member,
                                             NULL);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    status = clientbus.AddMatch("type='signal',interface='org.alljoyn.alljoyn_test.interface1',member='my_signal'");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    EXPECT_FALSE(clientProxyObject.IsSecure());
    status = clientProxyObject.SecureConnection();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(clientProxyObject.IsSecure());

    status = serviceObject.SendSignal();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    //Wait for a maximum of 2 sec for signal to be arrived
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(signalReceiver.signalReceived);
    ASSERT_TRUE(signalReceiver.msgEncrypted);

}


/*
 *  signal sender object level = true.
 *  service creates interface with OFF.
 *  Signal is not encrypted.
 */

TEST_F(ObjectSecurityTest, DISABLED_Test29) {

    QStatus status = ER_OK;

    InterfaceDescription* servicetestIntf = NULL;
    status = servicebus.CreateInterface(interface1, servicetestIntf, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = servicetestIntf->AddSignal("my_signal", "s", NULL, 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    servicetestIntf->Activate();

    SignalSecurityTestObject serviceObject(object_path, *servicetestIntf);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_OFF);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddSignal("my_signal", "s", NULL, 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    const InterfaceDescription::Member*  signal_member = clienttestIntf->GetMember("my_signal");

    SignalReceiver signalReceiver;
    status = clientbus.RegisterSignalHandler(&signalReceiver,
                                             static_cast<MessageReceiver::SignalHandler>(&SignalReceiver::SignalHandler),
                                             signal_member,
                                             NULL);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    status = clientbus.AddMatch("type='signal',interface='org.alljoyn.alljoyn_test.interface1',member='my_signal'");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    EXPECT_FALSE(clientProxyObject.IsSecure());
    status = clientProxyObject.SecureConnection();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(clientProxyObject.IsSecure());

    status = serviceObject.SendSignal();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    //Wait for a maximum of 2 sec for signal to be arrived
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(signalReceiver.signalReceived);
    ASSERT_FALSE(signalReceiver.msgEncrypted);

}


/*
 *  signal sender object level = true.
 *  service creates interface with INHERIT.
 *  Signal is encrypted.
 */

TEST_F(ObjectSecurityTest, DISABLED_Test30) {

    QStatus status = ER_OK;

    InterfaceDescription* servicetestIntf = NULL;
    status = servicebus.CreateInterface(interface1, servicetestIntf, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = servicetestIntf->AddSignal("my_signal", "s", NULL, 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    servicetestIntf->Activate();

    SignalSecurityTestObject serviceObject(object_path, *servicetestIntf);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_INHERIT);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddSignal("my_signal", "s", NULL, 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    const InterfaceDescription::Member*  signal_member = clienttestIntf->GetMember("my_signal");

    SignalReceiver signalReceiver;
    status = clientbus.RegisterSignalHandler(&signalReceiver,
                                             static_cast<MessageReceiver::SignalHandler>(&SignalReceiver::SignalHandler),
                                             signal_member,
                                             NULL);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    status = clientbus.AddMatch("type='signal',interface='org.alljoyn.alljoyn_test.interface1',member='my_signal'");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    EXPECT_FALSE(clientProxyObject.IsSecure());
    status = clientProxyObject.SecureConnection();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(clientProxyObject.IsSecure());

    status = serviceObject.SendSignal();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    //Wait for a maximum of 2 sec for signal to be arrived
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(signalReceiver.signalReceived);
    ASSERT_TRUE(signalReceiver.msgEncrypted);

}



/*
 *  signal sender object level = true.
 *  service creates interface with INHERIT.
 *  Signal is encrypted.
 */

TEST_F(ObjectSecurityTest, DISABLED_Test31) {

    QStatus status = ER_OK;

    InterfaceDescription* servicetestIntf = NULL;
    status = servicebus.CreateInterface(interface1, servicetestIntf, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = servicetestIntf->AddSignal("my_signal", "s", NULL, 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    servicetestIntf->Activate();

    SignalSecurityTestObject serviceObject(object_path, *servicetestIntf);
    servicebus.RegisterBusObject(serviceObject, true);
    //Wait for a maximum of 2 sec for object to be registered
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }


    InterfaceDescription* clienttestIntf = NULL;
    status = clientbus.CreateInterface(interface1, clienttestIntf, AJ_IFC_SECURITY_REQUIRED);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    status = clienttestIntf->AddSignal("my_signal", "s", NULL, 0);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    clienttestIntf->Activate();
    const InterfaceDescription::Member*  signal_member = clienttestIntf->GetMember("my_signal");

    SignalReceiver signalReceiver;
    status = clientbus.RegisterSignalHandler(&signalReceiver,
                                             static_cast<MessageReceiver::SignalHandler>(&SignalReceiver::SignalHandler),
                                             signal_member,
                                             NULL);
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    status = clientbus.AddMatch("type='signal',interface='org.alljoyn.alljoyn_test.interface1',member='my_signal'");
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    ProxyBusObject clientProxyObject(clientbus, servicebus.GetUniqueName().c_str(), object_path, 0, false);
    EXPECT_FALSE(clientProxyObject.IsSecure());
    status = clientProxyObject.SecureConnection();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);
    EXPECT_TRUE(clientProxyObject.IsSecure());

    status = serviceObject.SendSignal();
    EXPECT_EQ(ER_OK, status) << "  Actual Status: " << QCC_StatusText(status);

    //Wait for a maximum of 2 sec for signal to be arrived
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(10);
        if (serviceObject.objectRegistered) {
            break;
        }
    }
    ASSERT_TRUE(signalReceiver.signalReceived);
    ASSERT_TRUE(signalReceiver.msgEncrypted);

}


