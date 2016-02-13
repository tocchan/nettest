/************************************************************************/
/*                                                                      */
/* INCLUDE                                                              */
/*                                                                      */
/************************************************************************/
#include "core/common.h"
#include "core/engine.h"
#include "core/time/time.h"

#include "net/net.h"
#include "net/address.h"
#include "net/message.h"
#include "net/session.h"

#include <conio.h>

/************************************************************************/
/*                                                                      */
/* DEFINES AND CONSTANTS                                                */
/*                                                                      */
/************************************************************************/
uint32_t const cMessagesPerPort = 5;
uint32_t const cStartPort = 12345;
uint_t const cMaxTimeToWait_ms = 5000;

/************************************************************************/
/*                                                                      */
/* MACROS                                                               */
/*                                                                      */
/************************************************************************/

/************************************************************************/
/*                                                                      */
/* TYPES                                                                */
/*                                                                      */
/************************************************************************/

/************************************************************************/
/*                                                                      */
/* STRUCTS                                                              */
/*                                                                      */
/************************************************************************/
enum eGamePackets : uint8_t
{
   eGamePacket_Ping = eNetMessage_CORE_MESSAGE_COUNT,
   eGamePacket_Pong,
   eGamePacket_Next,
   eGamePacket_Start,
};

/************************************************************************/
/*                                                                      */
/* CLASSES                                                              */
/*                                                                      */
/************************************************************************/

/************************************************************************/
/*                                                                      */
/* LOCAL VARIABLES                                                      */
/*                                                                      */
/************************************************************************/
static uint32_t gPingsReceived = 0;
static uint32_t gPongsReceived = 0;
static uint16_t gNextPort = 1024;
static net_address_t gClientAddr;
static uint_t gLastReceivedTime;

/************************************************************************/
/*                                                                      */
/* GLOBAL VARIABLES                                                     */
/*                                                                      */
/************************************************************************/

/************************************************************************/
/*                                                                      */
/* LOCAL FUNCTIONS                                                      */
/*                                                                      */
/************************************************************************/
//------------------------------------------------------------------------
NET_MESSAGE( test_ping, eGamePacket_Ping ) 
{
   CNetMessage response( eGamePacket_Pong );
   NetMessageSendDirect( from.session, from.addr, response );
   ++gPingsReceived;
}

//------------------------------------------------------------------------
NET_MESSAGE( test_pong, eGamePacket_Pong )
{
   ++gPongsReceived;
   gLastReceivedTime = TimeGet_ms();
}

//------------------------------------------------------------------------
NET_MESSAGE( test_next, eGamePacket_Next )
{
   msg.read<uint16_t>(&gNextPort);
   gLastReceivedTime = TimeGet_ms();
}

NET_MESSAGE( test_start, eGamePacket_Start )
{
   Trace("server", "Received Start Message.");
   gClientAddr = from.addr;
}

//------------------------------------------------------------------------
static void StartAsServer( uint16_t min_port, uint16_t max_port )
{
   Trace( "server", "Starting as server.  Testing port range [%u -> %u]", min_port, max_port );

   net_address_t my_addr;
   NetAddressForMe( &my_addr, 1, eAF_IPv4, cStartPort );
   char str[128];
   NetAddressToString( str, 128, my_addr );

   CNetSession *sp = NetSessionCreate();
   
   // Wait for a client to make first contact before moving on to the next step
   Trace( "server", "Waiting for client at: %s", str );
   NetAddressClean( &gClientAddr );
   NetSessionStart( sp, cStartPort );
   while (!NetAddressIsValid(gClientAddr)) {
      NetSystemStep();
      ThreadSleep(100);
   }

   char client_name[128];
   NetAddressToString( client_name, 128, gClientAddr );
   Trace( "server", "Client started test from: %s", client_name );
   NetSessionStop(sp);

   // Okay, we know who we're talking to
   for (uint16_t port = min_port; port <= max_port; ++port) {
      gPingsReceived = 0;

      NetSessionStart( sp, port );


      // Okay, now wait for messages.
      uint_t start_time = TimeGet_ms();
      while (true) {
         // Tell the client to send to this port
         CNetMessage start( eGamePacket_Next );
         start.write<uint16_t>(port);
         NetMessageSendDirect( sp, gClientAddr, start );

         NetSystemStep();

         uint_t curr_time = TimeGet_ms();
         if ((curr_time - start_time) > cMaxTimeToWait_ms) {
            Trace("server", "Received no traffic on port[%u]", port );
            break;
         }

         if (!SocketIsRunning(sp->socket)) {
            Trace("server", "Socket unexpectantly quit on port[%u]", port );
            break;
         }

         if (gPingsReceived >= cMessagesPerPort) {
            Trace( "server", "Port[%u] received %u messages.  Passed.", port, gPingsReceived );
            break;
         }

         ThreadSleep(100);
      }

      NetSessionStop(sp);
   }

   NetSessionDestroy(sp);
   Trace( "server", "Server Finished." );

}

//------------------------------------------------------------------------
static void StartAsClient( char const *host )
{
   Trace( "client", "Starting as Client - trying to connect to %s", host );

   net_address_t addr;
   size_t found = NetAddressForHost( &addr, 1, eAF_IPv4, host, cStartPort, eFindAddr_NumericOnly );
   if (found == 0) {
      Trace( "Client", "Could not find a host address." );
      return;
   }

   gNextPort = 0;
   uint16_t cur_port = gNextPort;

   CNetSession *sp = NetSessionCreate();
   NetSessionStart( sp, cStartPort );

   CNetMessage start( eGamePacket_Start );
   NetMessageSendDirect( sp, addr, start );

   gLastReceivedTime = TimeGet_ms();
   while (true) {
      NetSystemStep();

      uint_t cur_time = TimeGet_ms();
      if ((cur_time - gLastReceivedTime) > 30000) {
         Trace( "client", "Disconnected, no traffic in 30 seconds." );
         break;
      }
      
      if (gNextPort != cur_port) {
         cur_port = gNextPort;
         Trace( "client", "Testing port[%u]", cur_port );
      }

      if (cur_port > 0) {
         addr.port = gNextPort;
         CNetMessage ping( eGamePacket_Ping );
         NetMessageSendDirect( sp, addr, ping );
      }

      ThreadSleep(10);
   }

   NetSessionDestroy(sp);
   Trace( "client", "Client finished." );
}

/************************************************************************/
/*                                                                      */
/* EXTERNAL FUNCTIONS                                                   */
/*                                                                      */
/************************************************************************/
#include <stdio.h>

int main( int32_t argc, char const **argv )
{
   EngineInit( nullptr, nullptr );
   NetSystemInit();

   bool server = true;
   uint16_t min_port = 1024;
   uint16_t max_port = 15000;
   char host_name[128];


   if (argc <= 1) {
      printf( "Server [0] or Client [1]? " );
      int response;
      scanf( "%i", &response );
      server = (response == 0);
      if (server) {
         printf( "Enter Min Port: " );
         scanf( "%hu", &min_port );
         printf( "Enter Max Port: " );
         scanf( "%hu", &max_port );
      } else {
         printf( "Enter Host IP: " );
         scanf( "%s", host_name );
      }
   } if (argc > 2) {
      min_port = atoi(argv[1]);
      max_port = atoi(argv[2]);
   } else if (argc == 2) {
      strcpy_s( host_name, 128, argv[1] );
   }

   if (!server) {
      StartAsClient(host_name);
   } else {
      StartAsServer(min_port, max_port);
   }

   NetSystemDeinit();
   EngineDeinit();
   return 0;
}