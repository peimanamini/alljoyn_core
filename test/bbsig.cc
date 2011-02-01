/**
 * @file
 * Message Bus Client
 */

/******************************************************************************
 * Copyright 2009-2011, Qualcomm Innovation Center, Inc.
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
#include <qcc/platform.h>

#include <signal.h>
#include <stdio.h>
#include <assert.h>

#include <vector>

#include <qcc/Debug.h>
#include <qcc/String.h>
#include <qcc/Environ.h>
#include <qcc/Util.h>
#include <qcc/Thread.h>

#include <alljoyn/BusAttachment.h>
#include <alljoyn/BusObject.h>
#include <alljoyn/DBusStd.h>
#include <alljoyn/AllJoynStd.h>
#include <alljoyn/version.h>

#include <Status.h>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#define QCC_MODULE "ALLJOYN"

using namespace std;
using namespace qcc;
using namespace ajn;

/** Constants */
namespace org {
namespace alljoyn {
namespace alljoyn_test {
const char* InterfaceName = "org.alljoyn.alljoyn_test";
const char* DefaultWellKnownName = "org.alljoyn.alljoyn_test";
const char* ObjectPath = "/org/alljoyn/alljoyn_test";
}
}
}

/** Static top level message bus object */
static BusAttachment* g_msgBus = NULL;
static String g_wellKnownName = ::org::alljoyn::alljoyn_test::DefaultWellKnownName;
static Event g_discoverEvent;

static bool stopNow = false;

static bool compress = false;
static bool encryption = false;
static unsigned long timeToLive = 0;

/** AllJoynListener receives discovery events from AllJoyn */
class MyBusListener : public BusListener {
  public:
    void FoundName(const char* name, const char* guid, const char* namePrefix, const char* busAddress)
    {
        QCC_SyncPrintf("FoundName(name=%s, guid=%s, addr=%s)\n", name, guid, busAddress);

        if (0 == strcmp(name, g_wellKnownName.c_str())) {
            /* We found a remote bus that is advertising bbservice's well-known name so connect to it */
            uint32_t disposition;
            QStatus status = g_msgBus->ConnectToRemoteBus(busAddress, disposition);
            if ((ER_OK == status) && (ALLJOYN_CONNECT_REPLY_SUCCESS == disposition)) {
                /* Release main thread */
                g_discoverEvent.SetEvent();
            } else {
                QCC_LogError(status, ("ConnectToRemoteBus failed (status=%s, disposition=%d)", QCC_StatusText(status), disposition));
            }
        }
    }

    void NameOwnerChanged(const char* name, const char* previousOwner, const char* newOwner)
    {
        QCC_SyncPrintf("NameOwnerChanged(%s, %s, %s)\n",
                       name,
                       previousOwner ? previousOwner : "null",
                       newOwner ? newOwner : "null");

        if (newOwner && (0 == strcmp(name, g_wellKnownName.c_str()))) {
            /* Inform main thread that name is available */
            g_discoverEvent.SetEvent();
        }
    }
};

/** Static bus listener */
static MyBusListener g_busListener;


/** Signal handler */
static void SigIntHandler(int sig)
{
    if (!stopNow) {
        stopNow = true;
        if (NULL != g_msgBus) {
            QStatus status = g_msgBus->Stop(false);
            if (ER_OK != status) {
                QCC_LogError(status, ("BusAttachment::Stop() failed"));
            }
        }
    }
}

class LocalTestObject : public BusObject {

  public:

    LocalTestObject(BusAttachment& bus, const char* path, unsigned long signalDelay, unsigned long disconnectDelay,
                    unsigned long reportInterval, unsigned long maxSignals) :
        BusObject(bus, path),
        signalDelay(signalDelay),
        disconnectDelay(disconnectDelay),
        reportInterval(reportInterval),
        maxSignals(maxSignals),
        my_signal_member(NULL)
    {
        QStatus status;

        /* Add org.alljoyn.alljoyn_test interface */
        InterfaceDescription* testIntf = NULL;
        status = bus.CreateInterface(::org::alljoyn::alljoyn_test::InterfaceName, testIntf);
        if (ER_OK == status) {
            testIntf->AddSignal("my_signal", "a{ys}", NULL, 0);
            testIntf->Activate();
        } else {
            QCC_LogError(status, ("Failed to create interface %s", ::org::alljoyn::alljoyn_test::InterfaceName));
        }

        /* Get my_signal member */
        if (ER_OK == status) {
            my_signal_member = testIntf->GetMember("my_signal");
            assert(my_signal_member);

            status =  bus.RegisterSignalHandler(this,
                                                static_cast<MessageReceiver::SignalHandler>(&LocalTestObject::SignalHandler),
                                                my_signal_member,
                                                NULL);

            if (status != ER_OK) {
                QCC_SyncPrintf("Failed to register signal handler for 'org.alljoyn.alljoyn_test.my_signal': %s\n",
                               QCC_StatusText(status));
            }
        }
    }

    QStatus SendSignal() {
        uint8_t flags = ALLJOYN_FLAG_GLOBAL_BROADCAST;
        assert(my_signal_member);
        MsgArg arg("a{ys}", 0, NULL);
        if (compress) {
            flags |= ALLJOYN_FLAG_COMPRESSED;
        }
        if (encryption) {
            flags |= ALLJOYN_FLAG_ENCRYPTED;
        }
        return Signal(NULL, *my_signal_member, &arg, 1, timeToLive, flags);
    }

    void SignalHandler(const InterfaceDescription::Member* member,
                       const char* sourcePath,
                       Message& msg)
    {
        if ((++rxCounts[sourcePath] % reportInterval) == 0) {
            QCC_SyncPrintf("RxSignal: %s - %u\n", sourcePath, rxCounts[sourcePath]);
            if (msg->IsEncrypted()) {
                QCC_SyncPrintf("Authenticated using %s\n", msg->GetAuthMechanism().c_str());
            }
        }
    }

    void RegisterSignalHandler()
    {
        QStatus status;
        MsgArg arg("s", "type='signal',interface='org.alljoyn.alljoyn_test',member='my_signal'");
        Message reply(bus);
        const ProxyBusObject& dbusObj = bus.GetDBusProxyObj();

        status = dbusObj.MethodCall(ajn::org::freedesktop::DBus::InterfaceName,
                                    "AddMatch",
                                    &arg,
                                    1,
                                    reply);
        if (status != ER_OK) {
            QCC_SyncPrintf("Failed to register Match rule for 'org.alljoyn.alljoyn_test.my_signal': %s\n",
                           QCC_StatusText(status));
            QCC_SyncPrintf("reply msg: %s\n", reply->ToString().c_str());
        }
    }


    void ObjectRegistered(void)
    {
        BusObject::ObjectRegistered();

        /* Request a well-known name */
        /* Note that you cannot make a blocking method call here */
        const ProxyBusObject& dbusObj = bus.GetDBusProxyObj();
        MsgArg args[2];
        args[0].Set("s", g_wellKnownName.c_str());
        args[1].Set("u", 6);
        QStatus status = dbusObj.MethodCallAsync(ajn::org::freedesktop::DBus::InterfaceName,
                                                 "RequestName",
                                                 this,
                                                 static_cast<MessageReceiver::ReplyHandler>(&LocalTestObject::NameAcquiredCB),
                                                 args,
                                                 ArraySize(args));
        if (ER_OK != status) {
            QCC_LogError(status, ("Failed to request name %s", g_wellKnownName.c_str()));
        }
    }

    void NameAcquiredCB(Message& msg, void* context)
    {
        /* Check name acquired result */
        size_t numArgs;
        const MsgArg* args;
        msg->GetArgs(numArgs, args);

        if (args[0].v_uint32 == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
            /* Begin Advertising the well known name to remote busses */
            const ProxyBusObject& alljoynObj = bus.GetAllJoynProxyObj();
            MsgArg arg("s", g_wellKnownName.c_str());
            QStatus status = alljoynObj.MethodCallAsync(ajn::org::alljoyn::Bus::InterfaceName,
                                                        "AdvertiseName",
                                                        this,
                                                        static_cast<MessageReceiver::ReplyHandler>(&LocalTestObject::AdvertiseRequestCB),
                                                        &arg,
                                                        1);
            if (ER_OK != status) {
                QCC_LogError(status, ("Sending org.alljoyn.Bus.Advertise failed"));
            }
        } else {
            QCC_LogError(ER_FAIL, ("Failed to obtain name \"%s\". RequestName returned %d",
                                   g_wellKnownName.c_str(), args[0].v_uint32));
        }
    }

    void AdvertiseRequestCB(Message& msg, void* context)
    {
        /* Make sure request was processed */
        size_t numArgs;
        const MsgArg* args;
        msg->GetArgs(numArgs, args);

        if ((MESSAGE_METHOD_RET != msg->GetType()) || (ALLJOYN_ADVERTISENAME_REPLY_SUCCESS != args[0].v_uint32)) {
            QCC_LogError(ER_FAIL, ("Failed to advertise name \"%s\". org.alljoyn.Bus.Advertise returned %d",
                                   g_wellKnownName.c_str(),
                                   args[0].v_uint32));
        }
    }


    map<qcc::String, size_t> rxCounts;

    unsigned long signalDelay;
    unsigned long disconnectDelay;
    unsigned long reportInterval;
    unsigned long maxSignals;
    const InterfaceDescription::Member* my_signal_member;
};


static const char x509cert[] = {
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBszCCARwCCQDuCh+BWVBk2DANBgkqhkiG9w0BAQUFADAeMQ0wCwYDVQQKDARN\n"
    "QnVzMQ0wCwYDVQQDDARHcmVnMB4XDTEwMDUxNzE1MTg1N1oXDTExMDUxNzE1MTg1\n"
    "N1owHjENMAsGA1UECgwETUJ1czENMAsGA1UEAwwER3JlZzCBnzANBgkqhkiG9w0B\n"
    "AQEFAAOBjQAwgYkCgYEArSd4r62mdaIRG9xZPDAXfImt8e7GTIyXeM8z49Ie1mrQ\n"
    "h7roHbn931Znzn20QQwFD6pPC7WxStXJVH0iAoYgzzPsXV8kZdbkLGUMPl2GoZY3\n"
    "xDSD+DA3m6krcXcN7dpHv9OlN0D9Trc288GYuFEENpikZvQhMKPDUAEkucQ95Z8C\n"
    "AwEAATANBgkqhkiG9w0BAQUFAAOBgQBkYY6zzf92LRfMtjkKs2am9qvjbqXyDJLS\n"
    "viKmYe1tGmNBUzucDC5w6qpPCTSe23H2qup27///fhUUuJ/ssUnJ+Y77jM/u1O9q\n"
    "PIn+u89hRmqY5GKHnUSZZkbLB/yrcFEchHli3vLo4FOhVVHwpnwLtWSpfBF9fWcA\n"
    "7THIAV79Lg==\n"
    "-----END CERTIFICATE-----"
};

static const char privKey[] = {
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "Proc-Type: 4,ENCRYPTED\n"
    "DEK-Info: AES-128-CBC,0AE4BAB94CEAA7829273DD861B067DBA\n"
    "\n"
    "LSJOp+hEzNDDpIrh2UJ+3CauxWRKvmAoGB3r2hZfGJDrCeawJFqH0iSYEX0n0QEX\n"
    "jfQlV4LHSCoGMiw6uItTof5kHKlbp5aXv4XgQb74nw+2LkftLaTchNs0bW0TiGfQ\n"
    "XIuDNsmnZ5+CiAVYIKzsPeXPT4ZZSAwHsjM7LFmosStnyg4Ep8vko+Qh9TpCdFX8\n"
    "w3tH7qRhfHtpo9yOmp4hV9Mlvx8bf99lXSsFJeD99C5GQV2lAMvpfmM8Vqiq9CQN\n"
    "9OY6VNevKbAgLG4Z43l0SnbXhS+mSzOYLxl8G728C6HYpnn+qICLe9xOIfn2zLjm\n"
    "YaPlQR4MSjHEouObXj1F4MQUS5irZCKgp4oM3G5Ovzt82pqzIW0ZHKvi1sqz/KjB\n"
    "wYAjnEGaJnD9B8lRsgM2iLXkqDmndYuQkQB8fhr+zzcFmqKZ1gLRnGQVXNcSPgjU\n"
    "Y0fmpokQPHH/52u+IgdiKiNYuSYkCfHX1Y3nftHGvWR3OWmw0k7c6+DfDU2fDthv\n"
    "3MUSm4f2quuiWpf+XJuMB11px1TDkTfY85m1aEb5j4clPGELeV+196OECcMm4qOw\n"
    "AYxO0J/1siXcA5o6yAqPwPFYcs/14O16FeXu+yG0RPeeZizrdlv49j6yQR3JLa2E\n"
    "pWiGR6hmnkixzOj43IPJOYXySuFSi7lTMYud4ZH2+KYeK23C2sfQSsKcLZAFATbq\n"
    "DY0TZHA5lbUiOSUF5kgd12maHAMidq9nIrUpJDzafgK9JrnvZr+dVYM6CiPhiuqJ\n"
    "bXvt08wtKt68Ymfcx+l64mwzNLS+OFznEeIjLoaHU4c=\n"
    "-----END RSA PRIVATE KEY-----"
};

class MyAuthListener : public AuthListener {
  public:

    MyAuthListener(const qcc::String& userName, unsigned long maxAuth) : AuthListener(), userName(userName), maxAuth(maxAuth) { }

  private:

    bool RequestCredentials(const char* authMechanism, uint16_t authCount, const char* userId, uint16_t credMask, Credentials& creds) {

        if (authCount > maxAuth) {
            return false;
        }

        if (strcmp(authMechanism, "ALLJOYN_SRP_KEYX") == 0) {
            if (credMask & AuthListener::CRED_PASSWORD) {
                creds.SetPassword("123456");
                printf("AuthListener returning fixed pin \"%s\" for %s\n", creds.GetPassword().c_str(), authMechanism);
            }
            return true;
        }

        if (strcmp(authMechanism, "ALLJOYN_RSA_KEYX") == 0) {
            if (credMask & AuthListener::CRED_CERT_CHAIN) {
                creds.SetCertChain(x509cert);
            }
            if (credMask & AuthListener::CRED_PRIVATE_KEY) {
                creds.SetPrivateKey(privKey);
            }
            if (credMask & AuthListener::CRED_PASSWORD) {
                creds.SetPassword("123456");
            }
            return true;
        }

        if (strcmp(authMechanism, "ALLJOYN_SRP_LOGON") == 0) {
            if (credMask & AuthListener::CRED_USER_NAME) {
                creds.SetUserName(userName);
            }
            if (credMask & AuthListener::CRED_PASSWORD) {
                creds.SetPassword("123456");
            }
            return true;
        }

        return false;
    }

    bool VerifyCredentials(const char* authMechanism, const Credentials& creds) {
        if (strcmp(authMechanism, "ALLJOYN_RSA_KEYX") == 0) {
            if (creds.IsSet(AuthListener::CRED_CERT_CHAIN)) {
                printf("Verify\n%s\n", creds.GetCertChain().c_str());
                return true;
            }
        }
        return false;
    }

    void AuthenticationComplete(const char* authMechanism, bool success) {
        printf("Authentication %s %s\n", authMechanism, success ? "succesful" : "failed");
    }

    void SecurityViolation(const char* error) {
        printf("Security violation %s\n", error);
    }

    qcc::String userName;
    unsigned long maxAuth;
};

static void usage(void)
{
    printf("Usage: bbsig [-n <name> ] [-h] [-l] [-s] [-r #] [-i #] [-c #] [-t #] [-x] [-e[k] <mech>]\n\n");
    printf("Options:\n");
    printf("   -h              = Print this help message\n");
    printf("   -n <name>       = Well-known name to advertise\n");
    printf("   -s              = Enable stress mode (connect/disconnect w/ server between runs non-stop)\n");
    printf("   -l              = Register signal handler for loopback\n");
    printf("   -r #            = Signal rate (delay in ms between signals sent; default = 0)\n");
    printf("   -d #            = Delay (in ms) between sending last signal and disconnecting (stress mode only)\n");
    printf("   -i #            = Signal report interval (number of signals tx/rx per update; default = 1000)\n");
    printf("   -c #            = Max number of signals to send, default = 1000000)\n");
    printf("   -t #            = TTL for the signals\n");
    printf("   -x              = Compress headers\n");
    printf("   -e[k] [RSA|SRP] = Encrypt the test interface using specified auth mechanism, -ek means clear keys\n");
    printf("   -d              = discover remote bus with test service\n");
    printf("   -w              = Wait for a CTRL-C to stop the bus\n");
}


/** Main entry point */
int main(int argc, char** argv)
{
    QStatus status = ER_OK;

    bool clearKeys = false;
    qcc::String userId;
    qcc::String authMechs;
    unsigned long authCount = 1000;

    bool doStress = false;
    bool waitStop = false;
    bool useSignalHandler = false;
    bool discoverRemote = false;
    unsigned int pid;
    char objPathTemplate[] = "/org/alljoyn/AllJoyn/%u/test";
    char objPath[sizeof(objPathTemplate) + sizeof("65535")];
    unsigned long signalDelay = 0;
    unsigned long disconnectDelay = 0;
    unsigned long reportInterval = 1000;
    unsigned long maxSignals = 1000000;

#ifdef _WIN32
    WSADATA wsaData;
    WORD version = MAKEWORD(2, 0);
    int error = WSAStartup(version, &wsaData);
#endif

    printf("AllJoyn Library version: %s\n", ajn::GetVersion());
    printf("AllJoyn Library build info: %s\n", ajn::GetBuildInfo());

    /* Install SIGINT handler */
    signal(SIGINT, SigIntHandler);

#ifdef _WIN32
    pid = _getpid();
    _snprintf(objPath, sizeof(objPath), objPathTemplate, pid);
#else
    pid = getpid();
    snprintf(objPath, sizeof(objPath), objPathTemplate, pid);
#endif

    /* Parse command line args */
    for (int i = 1; i < argc; ++i) {
        if (0 == strcmp("-h", argv[i])) {
            usage();
            exit(0);
        } else if (0 == strcmp("-s", argv[i])) {
            doStress = true;
        } else if (0 == strcmp("-n", argv[i])) {
            ++i;
            if (i == argc) {
                printf("option %s requires a parameter\n", argv[i - 1]);
                usage();
                exit(1);
            } else {
                g_wellKnownName = argv[i];
            }

        } else if (0 == strcmp("-l", argv[i])) {
            useSignalHandler = true;
        } else if (0 == strcmp("-d", argv[i])) {
            discoverRemote = true;
        } else if (0 == strcmp("-r", argv[i])) {
            ++i;
            if (i == argc) {
                printf("option %s requires a parameter\n", argv[i - 1]);
                usage();
                exit(1);
            } else {
                signalDelay = strtoul(argv[i], NULL, 10);
            }
        } else if (0 == strcmp("-d", argv[i])) {
            ++i;
            if (i == argc) {
                printf("option %s requires a parameter\n", argv[i - 1]);
                usage();
                exit(1);
            } else {
                disconnectDelay = strtoul(argv[i], NULL, 10);
            }
        } else if (0 == strcmp("-i", argv[i])) {
            ++i;
            if (i == argc) {
                printf("option %s requires a parameter\n", argv[i - 1]);
                usage();
                exit(1);
            } else {
                reportInterval = strtoul(argv[i], NULL, 10);
            }
        } else if (0 == strcmp("-c", argv[i])) {
            ++i;
            if (i == argc) {
                printf("option %s requires a parameter\n", argv[i - 1]);
                usage();
                exit(1);
            } else {
                maxSignals = strtoul(argv[i], NULL, 10);
            }
        } else if (0 == strcmp("-t", argv[i])) {
            ++i;
            if (i == argc) {
                printf("option %s requires a parameter\n", argv[i - 1]);
                usage();
                exit(1);
            } else {
                timeToLive = strtoul(argv[i], NULL, 10);
            }
        } else if (0 == strcmp("-x", argv[i])) {
            compress = true;
        } else if (0 == strcmp("-w", argv[i])) {
            waitStop = true;
        } else if ((0 == strcmp("-e", argv[i])) || (0 == strcmp("-ek", argv[i]))) {
            if (!authMechs.empty()) {
                authMechs += " ";
            }
            bool ok = false;
            encryption = true;
            clearKeys |= (argv[i][2] == 'k');
            ++i;
            if (i != argc) {
                if (strcmp(argv[i], "RSA") == 0) {
                    authMechs += "ALLJOYN_RSA_KEYX";
                    ok = true;
                } else if (strcmp(argv[i], "SRP") == 0) {
                    authMechs += "ALLJOYN_SRP_KEYX";
                    ok = true;
                } else if (strcmp(argv[i], "LOGON") == 0) {
                    if (++i == argc) {
                        printf("option %s LOGON requires a user id\n", argv[i - 2]);
                        usage();
                        exit(1);
                    }
                    authMechs += "ALLJOYN_SRP_LOGON";
                    userId = argv[i];
                    ok = true;
                }
            }
            if (!ok) {
                printf("option %s requires an auth mechanism \n", argv[i - 1]);
                usage();
                exit(1);
            }
        } else {
            status = ER_FAIL;
            printf("Unknown option %s\n", argv[i]);
            usage();
            exit(1);
        }
    }

    do {
        /* Create message bus */
        g_msgBus = new BusAttachment("bbsig", true);

        /* Get env vars */
        Environ* env = Environ::GetAppEnviron();
#ifdef _WIN32
        qcc::String connectArgs = env->Find("BUS_ADDRESS", "tcp:addr=127.0.0.1,port=9955");
#else
        // qcc::String connectArgs = env->Find("BUS_ADDRESS", "unix:path=/var/run/dbus/system_bus_socket");
        qcc::String connectArgs = env->Find("BUS_ADDRESS", "unix:abstract=alljoyn");
#endif

        /* Start the msg bus */
        status = g_msgBus->Start();
        if (ER_OK != status) {
            QCC_LogError(status, ("Bus::Start failed"));
            break;
        }

        if (encryption) {
            g_msgBus->EnablePeerSecurity(authMechs.c_str(), new MyAuthListener(userId, authCount));
            if (clearKeys) {
                g_msgBus->ClearKeyStore();
            }
        }

        /* Register a bus listener in order to get discovery indications */
        if (discoverRemote) {
            g_msgBus->RegisterBusListener(g_busListener);
        }

        /* Register object and start the bus */
        LocalTestObject* testObj = new LocalTestObject(*g_msgBus, objPath, signalDelay, disconnectDelay, reportInterval, maxSignals);
        g_msgBus->RegisterBusObject(*testObj);

        /* Connect to the bus */
        status = g_msgBus->Connect(connectArgs.c_str());
        if (ER_OK != status) {
            QCC_LogError(status, ("Connect to \"%s\" failed", connectArgs.c_str()));
            break;
        }

        if (discoverRemote) {
            /* Begin discovery on the well-known name of the service to be called */
            Message reply(*g_msgBus);
            const ProxyBusObject& alljoynObj = g_msgBus->GetAllJoynProxyObj();

            MsgArg serviceName("s", g_wellKnownName.c_str());
            status = alljoynObj.MethodCall(::ajn::org::alljoyn::Bus::InterfaceName,
                                           "FindName",
                                           &serviceName,
                                           1,
                                           reply,
                                           5000);
            if (ER_OK != status) {
                QCC_LogError(status, ("%s.FindName failed", ::ajn::org::alljoyn::Bus::InterfaceName));
            }
        }

        /*
         * If discovering, wait for the "FoundName" signal that tells us that we are connected to a
         * remote bus that is advertising bbservice's well-known name.
         */
        if (discoverRemote && (ER_OK == status)) {
            status = Event::Wait(g_discoverEvent);
        }

        /* Register the signal handler and start sending signals */
        if (ER_OK == status) {
            if (useSignalHandler) {
                testObj->RegisterSignalHandler();
            }
            for (uint32_t i = 0; i < testObj->maxSignals; i++) {
                if (((i + 1) % testObj->reportInterval) == 0) {
                    QCC_SyncPrintf("SendSignal: %u\n", i + 1);
                }
                QStatus status = testObj->SendSignal();
                if (status != ER_OK) {
                    QCC_LogError(status, ("Failed to send signal"));
                    break;
                }
                if (testObj->signalDelay > 0) {
                    qcc::Sleep(testObj->signalDelay);
                }
            }
            /* If we are not sending signals we wait for signals to be sent to us */
            if (useSignalHandler && (testObj->maxSignals == 0)) {
                qcc::Sleep(0xFFFFFFFF);
            }

        }

        if (testObj->disconnectDelay > 0) {
            qcc::Sleep(testObj->disconnectDelay);
        }

        if (waitStop) {
            g_msgBus->WaitStop();
        } else {
            /* Stop the bus */
            status = g_msgBus->Stop();
            if (ER_OK != status) {
                QCC_LogError(status, ("BusAttachment::Stop() failed"));
            }
        }

        /* Clean up the test object for the next stress loop iteration */
        delete testObj;

        /* Clean up msg bus for next stress loop iteration*/
        BusAttachment* deleteMe = g_msgBus;
        g_msgBus = NULL;
        delete deleteMe;

    } while ((ER_OK == status) && doStress && !stopNow);


    if (g_msgBus) {
        BusAttachment* deleteMe = g_msgBus;
        g_msgBus = NULL;
        delete deleteMe;
    }

    QCC_SyncPrintf("bbsig exiting with %d (%s)\n", status, QCC_StatusText(status));

    return (int) status;
}
