/**
 *  websock.c
 *
 *  (c) 2020 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 **/

#include <jansson.h>
#include <dirent.h>

#include "lib/json.h"

#include "poold.h"
//#include "config.h"

int cWebSock::wsLogLevel = LLL_ERR | LLL_WARN; // LLL_INFO | LLL_NOTICE | LLL_WARN | LLL_ERR
lws_context* cWebSock::context = 0;
char* cWebSock::msgBuffer = 0;
int cWebSock::msgBufferSize = 0;
int cWebSock::timeout = 0;
cWebSock::MsgType cWebSock::msgType = mtNone;
std::map<void*,cWebSock::Client> cWebSock::clients;
cMyMutex cWebSock::clientsMutex;
char* cWebSock::httpPath = 0;
char* cWebSock::loginToken = 0;

//***************************************************************************
// Init
//***************************************************************************

cWebSock::cWebSock(const char* aHttpPath)
{
   httpPath = strdup(aHttpPath);
}

cWebSock::~cWebSock()
{
   exit();

   free(loginToken);
   free(httpPath);
   free(msgBuffer);
}

int cWebSock::init(int aPort, int aTimeout)
{
   struct lws_context_creation_info info;
   const char* interface = 0;
   const char* certPath = 0;           // we're not using ssl
   const char* keyPath = 0;

   lws_set_log_level(wsLogLevel, writeLog);

   port = aPort;
   timeout = aTimeout;

   // setup websocket protocol

   protocols[0].name = "http-only";
   protocols[0].callback = callbackHttp;
   protocols[0].per_session_data_size = sizeof(SessionData);
   protocols[0].rx_buffer_size = 0;

   protocols[1].name = "pool";
   protocols[1].callback = callbackPool;
   protocols[1].per_session_data_size = 0;
   protocols[1].rx_buffer_size = 80*1024;

   protocols[2].name = 0;
   protocols[2].callback = 0;
   protocols[2].per_session_data_size = 0;
   protocols[2].rx_buffer_size = 0;

   // setup websocket context info

   memset(&info, 0, sizeof(info));
   info.port = port;
   info.iface = interface;
   info.protocols = protocols;
   info.ssl_cert_filepath = certPath;
   info.ssl_private_key_filepath = keyPath;
   info.ws_ping_pong_interval = timeout;
   info.gid = -1;
   info.uid = -1;
   info.options = 0;

   // create libwebsocket context representing this server

   context = lws_create_context(&info);

   if (!context)
   {
      tell(0, "Error: libwebsocket init failed");
      return fail;
   }

   tell(0, "Listener at port (%d) established", port);
   tell(1, "using libwebsocket version '%s'", lws_get_library_version());

   return success;
}

void cWebSock::writeLog(int level, const char* line)
{
   if (wsLogLevel & level)
      tell(2, "WS: %s", line);
}

int cWebSock::exit()
{
   // lws_context_destroy(context);  #TODO ?

   return success;
}

//***************************************************************************
// Service
//***************************************************************************

int cWebSock::service(int timeoutMs)
{
   lws_service(context, timeoutMs);
   return done;
}

//***************************************************************************
// Perform Data
//***************************************************************************

int cWebSock::performData(MsgType type)
{
   int count = 0;

   cMyMutexLock lock(&clientsMutex);

   msgType = type;

   for (auto it = clients.begin(); it != clients.end(); ++it)
   {
      if (!it->second.messagesOut.empty())
         count++;
   }

   if (count || msgType == mtPing)
      lws_callback_on_writable_all_protocol(context, &protocols[1]);

   return done;
}

//***************************************************************************
// Get Client Info of connection
//***************************************************************************

const char* getClientInfo(lws* wsi, std::string* clientInfo)
{
   char clientName[100+TB] = "unknown";
   char clientIp[50+TB] = "";

   if (wsi)
   {
      int fd = lws_get_socket_fd(wsi);

      if (fd)
         lws_get_peer_addresses(wsi, fd, clientName, sizeof(clientName), clientIp, sizeof(clientIp));
   }

   *clientInfo = clientName + std::string("/") + clientIp;

   return clientInfo->c_str();
}

//***************************************************************************
// HTTP Callback
//***************************************************************************

int cWebSock::callbackHttp(lws* wsi, lws_callback_reasons reason, void* user,
                           void* in, size_t len)
{
   SessionData* sessionData = (SessionData*)user;
   std::string clientInfo = "unknown";

   switch (reason)
   {
      case LWS_CALLBACK_CLOSED:
      {
         getClientInfo(wsi, &clientInfo);

         tell(2, "DEBUG: Got unecpected LWS_CALLBACK_CLOSED for client '%s' (%p)",
              clientInfo.c_str(), (void*)wsi);
         break;
      }
      case LWS_CALLBACK_HTTP_BODY:
      {
         const char* message = (const char*)in;
         int s = lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI);

         tell(0, "DEBUG: Got unecpected LWS_CALLBACK_HTTP_BODY with [%.*s] lws_hdr_total_length is (%d)",
              (int)len+1, message, s);
         break;
      }

      case LWS_CALLBACK_CLIENT_WRITEABLE:
      {
         tell(2, "HTTP: Client writeable");
         break;
      }

      case LWS_CALLBACK_HTTP_WRITEABLE:
      {
         int res;

         tell(3, "HTTP: LWS_CALLBACK_HTTP_WRITEABLE");

         // data to write?

         if (!sessionData->dataPending)
         {
            tell(1, "Info: No more session data pending");
            return -1;
         }

         int m = lws_get_peer_write_allowance(wsi);

         if (!m)
            tell(3, "right now, peer can't handle anything :o");
         else if (m != -1 && m < sessionData->payloadSize)
            tell(0, "peer can't handle %d but %d is needed", m, sessionData->payloadSize);
         else if (m != -1)
            tell(3, "all fine, peer can handle %d bytes", m);

         res = lws_write(wsi, (unsigned char*)sessionData->buffer+sizeLwsPreFrame,
                         sessionData->payloadSize, LWS_WRITE_HTTP);

         if (res < 0)
            tell(0, "Failed writing '%s'", sessionData->buffer+sizeLwsPreFrame);
         else
            tell(2, "WROTE '%s' (%d)", sessionData->buffer+sizeLwsPreFrame, res);

         free(sessionData->buffer);
         memset(sessionData, 0, sizeof(SessionData));

         if (lws_http_transaction_completed(wsi))
            return -1;

         break;
      }

      case LWS_CALLBACK_HTTP:
      {
         int res;
         char* file = 0;
         const char* url = (char*)in;

         memset(sessionData, 0, sizeof(SessionData));

         tell(2, "HTTP: Requested uri: (%ld) '%s'", len, url);

         // data or file request ...

         if (strncmp(url, "/data/", 6) == 0)
         {
            // data request

            res = dispatchDataRequest(wsi, sessionData, url);

            if (res < 0 || (res > 0 && lws_http_transaction_completed(wsi)))
               return -1;
         }
         else
         {
            // file request

            if (strcmp(url, "/") == 0)
               url = "index.html";

            // security, force httpPath path to inhibit access to the whole filesystem

            asprintf(&file, "%s/%s", httpPath, url);

            res = serveFile(wsi, file);
            free(file);

            if (res < 0)
            {
               tell(2, "HTTP: Failed, uri: '%s' (%d)", url, res);
               return -1;
            }

            tell(3, "HTTP: Done, uri: '%s'", url);
         }

         break;
      }

      case LWS_CALLBACK_HTTP_FILE_COMPLETION:
      {
         if (lws_http_transaction_completed(wsi))
            return -1;

         break;
      }

      case LWS_CALLBACK_PROTOCOL_INIT:
      case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
      case LWS_CALLBACK_FILTER_HTTP_CONNECTION:
      case LWS_CALLBACK_CLOSED_HTTP:
      case LWS_CALLBACK_WSI_CREATE:
      case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
      case LWS_CALLBACK_ADD_POLL_FD:
      case LWS_CALLBACK_DEL_POLL_FD:
      case LWS_CALLBACK_WSI_DESTROY:
      case LWS_CALLBACK_CHANGE_MODE_POLL_FD:
      case LWS_CALLBACK_LOCK_POLL:
      case LWS_CALLBACK_UNLOCK_POLL:
      case LWS_CALLBACK_GET_THREAD_ID:
      case LWS_CALLBACK_HTTP_BIND_PROTOCOL:
      case LWS_CALLBACK_HTTP_DROP_PROTOCOL:
#if LWS_LIBRARY_VERSION_MAJOR >= 3
      case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE:
      case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
#endif
         break;

      default:
         tell(1, "DEBUG: Unhandled 'callbackHttp' got (%d)", reason);
         break;
   }

   return 0;
}

//***************************************************************************
// Serve File
//***************************************************************************

int cWebSock::serveFile(lws* wsi, const char* path)
{
   const char* suffix = suffixOf(path ? path : "");
   const char* mime = "text/plain";

   // choose mime type based on the file extension

   if (!isEmpty(suffix))
   {
      if      (strcmp(suffix, "png") == 0)   mime = "image/png";
      else if (strcmp(suffix, "jpg") == 0)   mime = "image/jpeg";
      else if (strcmp(suffix, "jpeg") == 0)  mime = "image/jpeg";
      else if (strcmp(suffix, "gif") == 0)   mime = "image/gif";
      else if (strcmp(suffix, "svg") == 0)   mime = "image/svg+xml";
      else if (strcmp(suffix, "html") == 0)  mime = "text/html";
      else if (strcmp(suffix, "css") == 0)   mime = "text/css";
   }

   return lws_serve_http_file(wsi, path, mime, 0, 0);
}

//***************************************************************************
// Callback for pool Protocol
//***************************************************************************

int cWebSock::callbackPool(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len)
{
   std::string clientInfo = "unknown";

   tell(3, "DEBUG: 'callbackPool' got (%d)", reason);

   switch (reason)
   {
      case LWS_CALLBACK_SERVER_WRITEABLE:
      case LWS_CALLBACK_RECEIVE:
      case LWS_CALLBACK_ESTABLISHED:
      case LWS_CALLBACK_CLOSED:
      case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
      case LWS_CALLBACK_RECEIVE_PONG:
      {
         getClientInfo(wsi, &clientInfo);
         break;
      }

      default: break;
   }

   switch (reason)
   {
      case LWS_CALLBACK_SERVER_WRITEABLE:                     // data to client
      {
         if (msgType == mtPing)
         {
            char buffer[sizeLwsFrame];

            tell(4, "DEBUG: Write 'PING' to '%s' (%p)", clientInfo.c_str(), (void*)wsi);
               lws_write(wsi, (unsigned char*)buffer + sizeLwsPreFrame, 0, LWS_WRITE_PING);
         }

         else if (msgType == mtData)
         {
            while (!clients[wsi].messagesOut.empty() && !lws_send_pipe_choked(wsi))
            {
               int res;
               std::string msg;  // take a copy of the message for short mutex lock!

               // mutex lock context
               {
                  cMyMutexLock clock(&clientsMutex);
                  cMyMutexLock lock(&clients[wsi].messagesOutMutex);
                  msg = clients[wsi].messagesOut.front();
                  clients[wsi].messagesOut.pop();
               }

               int msgSize = strlen(msg.c_str());
               int neededSize = sizeLwsFrame + msgSize;
               char* newBuffer = 0;

               if (neededSize > msgBufferSize)
               {
                  if (!(newBuffer = (char*)realloc(msgBuffer, neededSize)))
                  {
                     tell(0, "Fatal: Can't allocate memory!");
                     break;
                  }

                  msgBuffer = newBuffer;
                  msgBufferSize = neededSize;
               }

               strncpy(msgBuffer + sizeLwsPreFrame, msg.c_str(), msgSize);

               tell(1, "DEBUG: Write (%d) <- %.*s -> to '%s' (%p)\n", msgSize, msgSize,
                    msgBuffer+sizeLwsPreFrame, clientInfo.c_str(), (void*)wsi);

               res = lws_write(wsi, (unsigned char*)msgBuffer + sizeLwsPreFrame, msgSize, LWS_WRITE_TEXT);

               if (res != msgSize)
                  tell(0, "Error: Only (%d) bytes of (%d) sended", res, msgSize);
            }
         }

         break;
      }

      case LWS_CALLBACK_RECEIVE:                           // data from client
      {
         json_t *oData, *oObject;
         json_error_t error;
         Event event;

         tell(3, "DEBUG: 'LWS_CALLBACK_RECEIVE' [%.*s]", (int)len, (const char*)in);

         if (!(oData = json_loadb((const char*)in, len, 0, &error)))
         {
            tell(0, "Error: Ignoring unexpeted client request [%.*s]", (int)len, (const char*)in);
            tell(0, "Error decoding json: %s (%s, line %d column %d, position %d)",
                 error.text, error.source, error.line, error.column, error.position);
            break;
         }

         char* message = strndup((const char*)in, len);

         event = cWebService::toEvent(getStringFromJson(oData, "event", "<null>"));
         oObject = json_object_get(oData, "object");

         tell(3, "DEBUG: Got '%s'", message);

         if (event == evLogin)                             // { "event" : "login", "object" : { "type" : "foo" } }
         {
            const char* token = getStringFromJson(oObject, "token", "");

            if (strcmp(token, loginToken) == 0)
               clients[wsi].type = ctWithLogin;
            else
               clients[wsi].type = ctActive;

            atLogin(wsi, message, clientInfo.c_str());
         }
         else if (event == evLogout)                       // { "event" : "logout", "object" : { } }
         {
            tell(3, "DEBUG: Got '%s'", message);
            clients[wsi].cleanupMessageQueue();
         }
         else if (clients[wsi].type == ctWithLogin)
         {
            Poold::messagesIn.push(message);
         }
         else
         {
            tell(1, "Debug: Ignoring data of client (%p) without login [%s]", (void*)wsi, message);
         }

         json_decref(oData);
         free(message);

         break;
      }

      case LWS_CALLBACK_ESTABLISHED:                       // someone connecting
      {
         tell(1, "Client '%s' connected (%p), ping time set to (%d)", clientInfo.c_str(), (void*)wsi, timeout);
         clients[wsi].wsi = wsi;
         clients[wsi].type = ctActive; // ctInactive;  #TODO ??
         clients[wsi].tftprio = 100;

         break;
      }

      case LWS_CALLBACK_CLOSED:                            // someone dis-connecting
      {
         atLogout(wsi, "Client disconnected", clientInfo.c_str());
         break;
      }

      case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
      {
         atLogout(wsi, "Client disconnected unsolicited", clientInfo.c_str());
         break;
      }

      case LWS_CALLBACK_RECEIVE_PONG:                      // ping / pong
      {
         tell(2, "DEBUG: Got 'PONG' from client '%s' (%p)", clientInfo.c_str(), (void*)wsi);

         break;
      }

      case LWS_CALLBACK_ADD_HEADERS:
      case LWS_CALLBACK_PROTOCOL_INIT:
      case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
#if LWS_LIBRARY_VERSION_MAJOR >= 3
      case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
      case LWS_CALLBACK_HTTP_BIND_PROTOCOL:
#endif
         break;

      default:
         tell(1, "DEBUG: Unhandled 'callbackPool' got (%d)", reason);
         break;
   }

   return 0;
}

//***************************************************************************
// At Login / Logout
//***************************************************************************

void cWebSock::atLogin(lws* wsi, const char* message, const char* clientInfo)
{
   json_t* oContents = json_object();

   tell(1, "Client login '%s' (%p) [%s]", clientInfo, (void*)wsi, message);

   addToJson(oContents, "type", clients[wsi].type);
   addToJson(oContents, "client", (long)wsi);

   json_t* obj = json_object();
   addToJson(obj, "event", "login");
   json_object_set_new(obj, "object", oContents);

   char* p = json_dumps(obj, JSON_PRESERVE_ORDER);
   json_decref(obj);

   Poold::messagesIn.push(p);
   free(p);
}

void cWebSock::atLogout(lws* wsi, const char* message, const char* clientInfo)
{
   cMyMutexLock lock(&clientsMutex);

   tell(1, "%s '%s' (%p)", clientInfo, message, (void*)wsi);

   clients.erase(wsi);
}

//***************************************************************************
// Client Count
//***************************************************************************

int cWebSock::getClientCount()
{
   int count = 0;

   cMyMutexLock lock(&clientsMutex);

   for (auto it = clients.begin(); it != clients.end(); ++it)
   {
//      if (it->second.type != ctInactive)
         count++;
   }

   return count;
}

//***************************************************************************
// Push Message
//***************************************************************************

void cWebSock::pushMessage(const char* message, lws* wsi)
{
   cMyMutexLock lock(&clientsMutex);

   if (wsi)
   {
      if (clients.find(wsi) != clients.end())
         clients[wsi].pushMessage(message);
      else
         tell(0, "client %ld not found!", wsi);
   }
   else
   {
      for (auto it = clients.begin(); it != clients.end(); ++it)
      {
         // if (it->second.type != ctInactive)
            it->second.pushMessage(message);
      }
   }
}

//***************************************************************************
// Get Integer Parameter
//***************************************************************************

int cWebSock::getIntParameter(lws* wsi, const char* name, int def)
{
   char buf[100];
   const char* arg = lws_get_urlarg_by_name(wsi, name, buf, 100);

   return arg ? atoi(arg) : def;
}

//***************************************************************************
// Get String Parameter
//***************************************************************************

const char* cWebSock::getStrParameter(lws* wsi, const char* name, const char* def)
{
   static char buf[512+TB];
   const char* arg = lws_get_urlarg_by_name(wsi, name, buf, 512);

   return arg ? arg : def;
}

//***************************************************************************
// Method Of
//***************************************************************************

const char* cWebSock::methodOf(const char* url)
{
   const char* p;

   if (url && (p = strchr(url+1, '/')))
      return p+1;

   return "";
}

//***************************************************************************
// Dispatch Data Request
//***************************************************************************

int cWebSock::dispatchDataRequest(lws* wsi, SessionData* sessionData, const char* url)
{
   int statusCode = HTTP_STATUS_NOT_FOUND;

   const char* method = methodOf(url);

   // if (strcmp(method, "getenv") == 0)
   //    statusCode = doEnvironment(wsi, sessionData);

   return statusCode;
}
