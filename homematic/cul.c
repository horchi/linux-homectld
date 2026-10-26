//***************************************************************************
// HomeMatic (BidCos-RF) to MQTT
// File cul.c
// This code is distributed under the terms and conditions of the
// GNU GENERAL PUBLIC LICENSE. See the file LICENSE for details.
// Date 26.10.2026 - Jörg Wendel
//
// CUL with culfw (USB CDC, the baud rate doesn't matter), text lines:
//   V            -> version, e.g. 'V 1.67 CUL868'
//   X21          -> reporting on, with RSSI
//   Ar           -> BidCos (AskSin) receive mode, received frames as 'A<hex><rssi>'
//   As<hex>      -> send a BidCos frame
//   'LOVF'       -> duty cycle limit (1 %) reached, the frame was not sent
//***************************************************************************

#include <fcntl.h>
#include <termios.h>
#include <poll.h>

#include "hmmqtt.h"

//***************************************************************************
// Open / Close
//***************************************************************************

int Cul::open(const char* aDevice)
{
   close();
   device = aDevice;

   if ((fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0)
   {
      tell(eloAlways, "Error: Can't open CUL '%s', %s", device.c_str(), strerror(errno));
      return fail;
   }

   struct termios tio {};
   cfmakeraw(&tio);
   cfsetispeed(&tio, B38400);
   cfsetospeed(&tio, B38400);
   tio.c_cflag |= CLOCAL | CREAD;

   if (tcsetattr(fd, TCSANOW, &tio) != 0)
   {
      tell(eloAlways, "Error: Can't set up CUL '%s', %s", device.c_str(), strerror(errno));
      close();
      return fail;
   }

   tcflush(fd, TCIOFLUSH);
   buffer.clear();

   tell(eloAlways, "CUL '%s' opened", device.c_str());

   return success;
}

int Cul::close()
{
   if (fd >= 0)
      ::close(fd);

   fd = -1;
   return done;
}

//***************************************************************************
// Init
//***************************************************************************

int Cul::init()
{
   std::string line;

   version.clear();
   write("V");

   uint64_t until {cTimeMs::Now() + 2000};

   while (cTimeMs::Now() < until)
   {
      if (readLine(line, 200) == success && line.rfind("V ", 0) == 0)
      {
         version = line;
         break;
      }
   }

   if (version.empty())
   {
      tell(eloAlways, "Error: CUL '%s' doesn't answer the version request", device.c_str());
      return fail;
   }

   tell(eloAlways, "CUL version '%s'", version.c_str());

   write("X21");
   write("Ar");

   return success;
}

//***************************************************************************
// Send
//***************************************************************************

int Cul::send(const BidCos::Frame& frame)
{
   return write(("As" + frame.toHex()).c_str());
}

int Cul::write(const char* command)
{
   if (fd < 0)
      return fail;

   std::string line {std::string(command) + "\r\n"};

   tell(eloDebug, "CUL <- '%s'", command);

   if (::write(fd, line.c_str(), line.length()) != (ssize_t)line.length())
   {
      tell(eloAlways, "Error: Writing to CUL failed, %s", strerror(errno));
      close();
      return fail;
   }

   return success;
}

//***************************************************************************
// Read Line
//***************************************************************************

int Cul::readLine(std::string& line, int timeoutMs)
{
   line.clear();

   while (true)
   {
      size_t pos {buffer.find_first_of("\r\n")};

      if (pos != std::string::npos)
      {
         line = buffer.substr(0, pos);
         buffer.erase(0, buffer.find_first_not_of("\r\n", pos) == std::string::npos ? buffer.length()
                      : buffer.find_first_not_of("\r\n", pos));

         if (line.empty())
            continue;

         tell(eloDebug, "CUL -> '%s'", line.c_str());
         return success;
      }

      if (fd < 0)
         return fail;

      struct pollfd pfd {fd, POLLIN, 0};
      int res {::poll(&pfd, 1, timeoutMs)};

      if (res == 0)
         return wrnTimeout;

      if (res < 0)
         return errno == EINTR ? (int)wrnTimeout : (int)fail;

      if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
      {
         tell(eloAlways, "Error: CUL '%s' vanished", device.c_str());
         close();
         return fail;
      }

      char buf[256];
      ssize_t n {::read(fd, buf, sizeof(buf))};

      if (n <= 0)
      {
         if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return wrnTimeout;

         tell(eloAlways, "Error: Reading CUL '%s' failed, %s", device.c_str(), n ? strerror(errno) : "closed");
         close();
         return fail;
      }

      buffer.append(buf, n);
      timeoutMs = 0;                                   // got data, the rest of the line follows
   }
}
