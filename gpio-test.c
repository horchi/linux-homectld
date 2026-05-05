//***************************************************************************
// GPIO Wrapper Test
// File gpio-test.c
//
// Usage:
//   gpio-test --detect                              Enumerate all gpiochips + line names
//   gpio-test --list
//   gpio-test --input  <physPin> [--loops <n>] [--interval <ms>]
//   gpio-test --output <physPin> [--loops <n>] [--interval <ms>]
//   gpio-test --toggle <physPin> [--loops <n>] [--interval <ms>]
//   gpio-test --interrupt <physPin>
//   gpio-test --config <path>          (default: configs)
//
// Examples:
//   gpio-test --detect
//   gpio-test --list
//   gpio-test --input 11 --loops 10 --interval 500
//   gpio-test --output 7 --loops 6 --interval 200
//   gpio-test --interrupt 27
//***************************************************************************

#include <cstdio>
#include <cstring>
#include <csignal>
#include <chrono>
#include <thread>
#include <atomic>
#include <map>

#include "lib/common.h"
#include "gpio.h"

//***************************************************************************
// Signal handling
//***************************************************************************

static std::atomic<bool> interrupted {false};

static void onSignal(int) { interrupted = true; }

static void onGpioEdge(int physPin, bool value)
{
   printf("  [ISR] phys=%-3d value=%d\n", physPin, value ? 1 : 0);
}

//***************************************************************************
// Helpers
//***************************************************************************

static void printUsage(const char* prog)
{
   printf(
      "Usage:\n"
      "  %s --detect                                 Enumerate all gpiochips and line names\n"
      "  %s --list                                   List all pins from gpio.json\n"
      "  %s --input     <phys> [--loops n] [--interval ms]\n"
      "  %s --output    <phys> [--loops n] [--interval ms]\n"
      "  %s --toggle    <phys> [--loops n] [--interval ms]\n"
      "  %s --interrupt <phys>                       Wait for edge (Ctrl+C to stop)\n"
      "  %s --config    <path>                       Config path (default: configs)\n",
      prog, prog, prog, prog, prog, prog, prog);
}

//***************************************************************************
// Detect: enumerate all gpiochips and line names
//***************************************************************************

#if defined(GPIOD)
static int testDetect()
{
   bool anyChip {false};

   for (int i = 0; i < 16; ++i)
   {
      char path[32];
      snprintf(path, sizeof(path), "/dev/gpiochip%d", i);

      gpiod_chip* chip {gpiod_chip_open(path)};

      if (!chip)
         continue;

      anyChip = true;

      gpiod_chip_info* cinfo {gpiod_chip_get_info(chip)};
      const char* cname {cinfo ? gpiod_chip_info_get_name(cinfo)      : "?"};
      unsigned int nlines = cinfo ? gpiod_chip_info_get_num_lines(cinfo) : 0;

      printf("%-18s  name=%-20s  lines=%u\n", path, cname ? cname : "?", nlines);
      printf("  %5s  %-30s  %s\n", "Offs", "Line name", "Status");
      printf("  %.60s\n", "------------------------------------------------------------");

      for (unsigned int j = 0; j < nlines; ++j)
      {
         gpiod_line_info* linfo {gpiod_chip_get_line_info(chip, j)};

         if (!linfo)
            continue;

         const char* name {gpiod_line_info_get_name(linfo)};
         bool        used {gpiod_line_info_is_used(linfo)};
         const char* consumer {gpiod_line_info_get_consumer(linfo)};

         if (name && name[0])
         {
            printf("  %5u  %-30s  %s%s%s\n",
                   j, name,
                   used ? "used" : "free",
                   (used && consumer && consumer[0]) ? " by " : "",
                   (used && consumer && consumer[0]) ? consumer : "");
         }

         gpiod_line_info_free(linfo);
      }

      if (cinfo)
         gpiod_chip_info_free(cinfo);

      gpiod_chip_close(chip);
      printf("\n");
   }

   if (!anyChip)
      printf("No gpiochip devices found under /dev/gpiochip*\n");

   return 0;
}
#endif

//***************************************************************************
// List pins
//***************************************************************************

static int testList(Gpio& gpio)
{
   std::map<int, PinInfo> pins;

   if (gpio.getPinList(pins) != success)
   {
      printf("ERROR: getPinList() failed\n");
      return 1;
   }

   printf("%4s  %-14s  gpio  pull  int  %-5s  %s\n", "Phys", "Name", "Volt", "Description");
   printf("%.80s\n", "--------------------------------------------------------------------------------");

   for (const auto& [phys, info] : pins)
   {
      printf("%4d  %-14s  %-4s  %-4s  %s    %-5s  %s\n",
             phys,
             info.name.c_str(),
             info.gpio      ? "yes" : "no",
             info.pull      ? "yes" : "no",
             info.interrupt ? "yes" : "no",
             info.voltage.c_str(),
             info.description.c_str());
   }

   printf("\n%zu pin(s) loaded.\n", pins.size());
   return 0;
}

//***************************************************************************
// Input test
//***************************************************************************

static int testInput(Gpio& gpio, int phys, int loops, int intervalMs)
{
   printf("Setup phys %d as input (pullUp)...\n", phys);
   fflush(stdout);

   if (gpio.setupPin(phys, Gpio::dirIn, Gpio::edgeNone, Gpio::pudUp) != success)
   {
      printf("FAILED\n");
      return 1;
   }

   printf("OK\n");
   printf("Reading %d time(s) every %d ms:\n", loops, intervalMs);

   for (int i = 0; i < loops && !interrupted; ++i)
   {
      bool v {gpio.digitalRead(phys)};
      printf("  [%2d] phys %d = %d\n", i + 1, phys, v ? 1 : 0);
      std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
   }

   return 0;
}

//***************************************************************************
// Output test
//***************************************************************************

static int testOutput(Gpio& gpio, int phys, int loops, int intervalMs)
{
   printf("Setup phys %d as output...\n", phys);
   fflush(stdout);

   if (gpio.setupPin(phys, Gpio::dirOut, Gpio::edgeNone, Gpio::pudOff) != success)
   {
      printf("FAILED\n");
      return 1;
   }

   printf("OK\n");
   printf("Writing alternating 0/1, %d time(s) every %d ms:\n", loops, intervalMs);

   for (int i = 0; i < loops && !interrupted; ++i)
   {
      bool val {(i % 2 == 0)};
      int rc {gpio.digitalWrite(phys, val)};
      printf("  [%2d] digitalWrite(%d, %d) -> %s\n", i + 1, phys, val ? 1 : 0, rc == success ? "OK" : "FAILED");
      std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
   }

   gpio.digitalWrite(phys, false);
   printf("Pin set LOW on exit.\n");
   return 0;
}

//***************************************************************************
// Toggle test
//***************************************************************************

static int testToggle(Gpio& gpio, int phys, int loops, int intervalMs)
{
   printf("Setup phys %d as output for toggle test...\n", phys);
   fflush(stdout);

   if (gpio.setupPin(phys, Gpio::dirOut, Gpio::edgeNone, Gpio::pudOff) != success)
   {
      printf("FAILED\n");
      return 1;
   }

   printf("OK\n");
   gpio.digitalWrite(phys, false);

   printf("Toggling %d time(s) every %d ms:\n", loops, intervalMs);

   for (int i = 0; i < loops && !interrupted; ++i)
   {
      int  rc {gpio.digitalToggle(phys)};
      bool v {gpio.digitalRead(phys)};
      printf("  [%2d] toggle phys %d -> %d  %s\n", i + 1, phys, v ? 1 : 0, rc == success ? "OK" : "FAILED");
      std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
   }

   gpio.digitalWrite(phys, false);
   printf("Pin set LOW on exit.\n");
   return 0;
}

//***************************************************************************
// Interrupt test
//***************************************************************************

static int testInterrupt(Gpio& gpio, int phys)
{
   printf("Setup phys %d as input with edge=both + pullUp...\n", phys);
   fflush(stdout);

   if (gpio.setupPin(phys, Gpio::dirIn, Gpio::edgeBoth, Gpio::pudUp) != success)
   {
      printf("FAILED\n");
      return 1;
   }

   printf("OK\n");
   printf("Enabling interrupt...\n");
   fflush(stdout);

   if (gpio.enableInterrupt(phys, onGpioEdge) != success)
   {
      printf("FAILED\n");
      return 1;
   }
   printf("OK\n");

   printf("Waiting for edges on phys %d (Ctrl+C to stop)...\n", phys);

   while (!interrupted)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

   printf("\nDisabling interrupt...\n");
   gpio.disableInterrupt(phys);
   printf("OK\n");

   return 0;
}

//***************************************************************************
// Main
//***************************************************************************

int main(int argc, char* argv[])
{
   signal(SIGINT,  onSignal);
   signal(SIGTERM, onSignal);

   const char* configPath {"configs"};
   const char* mode {nullptr};
   int         physPin {-1};
   int         loops {10};
   int         intervalMs {500};

   for (int i = 1; i < argc; ++i)
   {
      if      (strcmp(argv[i], "--detect")    == 0) { mode = "detect"; }
      else if (strcmp(argv[i], "--list")      == 0) { mode = "list"; }
      else if (strcmp(argv[i], "--input")     == 0 && i + 1 < argc) { mode = "input";     physPin = atoi(argv[++i]); }
      else if (strcmp(argv[i], "--output")    == 0 && i + 1 < argc) { mode = "output";    physPin = atoi(argv[++i]); }
      else if (strcmp(argv[i], "--toggle")    == 0 && i + 1 < argc) { mode = "toggle";    physPin = atoi(argv[++i]); }
      else if (strcmp(argv[i], "--interrupt") == 0 && i + 1 < argc) { mode = "interrupt"; physPin = atoi(argv[++i]); }
      else if (strcmp(argv[i], "--config")    == 0 && i + 1 < argc) { configPath = argv[++i]; }
      else if (strcmp(argv[i], "--loops")     == 0 && i + 1 < argc) { loops      = atoi(argv[++i]); }
      else if (strcmp(argv[i], "--interval")  == 0 && i + 1 < argc) { intervalMs = atoi(argv[++i]); }
      else
      {
         printf("Unknown argument: %s\n", argv[i]);
         printUsage(argv[0]);
         return 1;
      }
   }

   if (!mode)
   {
      printUsage(argv[0]);
      return 1;
   }

#if defined(GPIOD)
   if (strcmp(mode, "detect") == 0)
      return testDetect();
#else
   if (strcmp(mode, "detect") == 0)
   {
      printf("--detect ist nur mit -DGPIOD verfügbar\n");
      return 1;
   }
#endif

   Gpio gpio("gpio-test", configPath);

   printf("Initializing GPIO (config: %s)...\n", configPath);

   if (gpio.init() != success)
   {
      printf("ERROR: gpio.init() failed\n");
      return 1;
   }

   printf("Init OK\n\n");

   if      (strcmp(mode, "list")      == 0) return testList(gpio);
   else if (strcmp(mode, "input")     == 0) return testInput(gpio, physPin, loops, intervalMs);
   else if (strcmp(mode, "output")    == 0) return testOutput(gpio, physPin, loops, intervalMs);
   else if (strcmp(mode, "toggle")    == 0) return testToggle(gpio, physPin, loops, intervalMs);
   else if (strcmp(mode, "interrupt") == 0) return testInterrupt(gpio, physPin);

   return 0;
}
