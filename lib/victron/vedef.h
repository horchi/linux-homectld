
// VE.Direct-Protocol-3.29.pdf
// https://www.victronenergy.com/upload/documents/VE.Direct-HEX-Protocol-Phoenix-Inverter.pdf
// https://www.victronenergy.com/live/open_source:start

#pragma once

//***************************************************************************
// ASCII Stream
//***************************************************************************

enum VeFormat
{
   vtString,
   vtInt,
   vtReal,
   vtBool
};

struct VePrettyData
{
   int address {na};      // address for the homectl daemon
   std::string title;
   std::string unit;
   bool incStructured {false};
   VeFormat format;
   uint factor {0};
};

enum VeDirectRegisterAddress
{
   vdraInfo = 900,
   vdraMode = 901,
};

// don't change existing addresses, append at the end!

static std::map<std::string,VePrettyData> vePrettyData
{
   // name,      addr,     titel,                  unit, struct,    format,      factor

   { "V",         {  0, "Voltage",                   "V",   true,    vtReal,      1000} },
   { "V2",        {  1, "Voltage 2",                 "V",   true,    vtReal,      1000} },
   { "V3",        {  2, "Voltage 3",                 "V",   true,    vtReal,      1000} },
   { "VS",        {  3, "Starter Voltage",           "V",   true,    vtReal,      1000} },
   { "VM",        {  4, "Mid Voltage",               "V",   true,    vtReal,      1000} },
   { "DM",        {  5, "Mid Deviation",             "%",   true,    vtReal,      10} },
   { "VPV",       {  6, "Panel Voltage",             "V",   true,    vtReal,      1000} },
   { "PPV",       {  7, "Panel Power",               "W",   true,    vtInt,       0} },
   { "I",         {  8, "Battery Current",           "A",   true,    vtReal,      1000} },
   { "I2",        {  9, "Battery Current 2",         "A",   true,    vtReal,      1000} },
   { "I3",        { 10, "Battery Current 3",         "A",   true,    vtReal,      1000} },
   { "IL",        { 11, "Load Current",              "A",   true,    vtReal,      1000} },
   { "LOAD",      { 12, "Load Output State",         "",    true,    vtString,    0} },
   { "T",         { 13, "Battery Temperature",       "°C",  true,    vtInt,       0} },
   { "P",         { 14, "Instantaneous Power",       "W",   true,    vtInt,       0} },
   { "CE",        { 15, "Consumed Amp Hours",        "Ah",  true,    vtReal,      1000} },
   { "SOC",       { 16, "SOC",                       "%",   true,    vtReal,      10} },
   { "TTG",       { 17, "Time to go",                "min", true,    vtInt,       0} },
   { "ALARM",     { 18, "Alarm",                     "",    true,    vtString,    0} },
   { "RELAY",     { 19, "Relay",                     "",    true,    vtString,    0} },
   { "AR",        { 20, "Alarm Code",                "",    true,    vtInt,       0} },
   { "OR",        { 21, "Off Reason",                "",    true,    vtInt,       0} },
   { "H1",        { 22, "Deepest Discharge",         "Ah",  true,    vtReal,      1000} },
   { "H2",        { 23, "Las Ddischarge",            "Ah",  true,    vtReal,      1000} },
   { "H3",        { 24, "Average Discharge",         "Ah",  true,    vtReal,      1000} },
   { "H4",        { 25, "Charge Cycles",             "",    true,    vtInt,       0} },
   { "H5",        { 26, "Full Discharges",           "",    true,    vtInt,       0} },
   { "H6",        { 27, "Cumulative Ah Drawn",       "Ah",  true,    vtReal,      1000} },
   { "H7",        { 28, "Minimum Voltage",           "V",   true,    vtReal,      1000} },
   { "H8",        { 29, "Maximum Voltage",           "V",   true,    vtReal,      1000} },
   { "H9",        { 30, "Last Full Charge",          "h",   true,    vtReal,      3600} },
   { "H10",       { 31, "Num Automatic Sync",        "",    true,    vtInt,       0} },
   { "H11",       { 32, "Low Volt Alarms",           "",    true,    vtInt,       0} },
   { "H12",       { 33, "High Volt Alarms",          "",    true,    vtInt,       0} },
   { "H13",       { 34, "Low AUX Volt Alarms",       "",    true,    vtInt,       0} },
   { "H14",       { 35, "High AUX Volt Alarms",      "",    true,    vtInt,       0} },
   { "H15",       { 36, "Min AUX Volt",              "V",   true,    vtReal,      1000} },
   { "H16",       { 37, "Max AUX Volt",              "V",   true,    vtReal,      1000} },
   { "H17",       { 38, "Amount Discharged Energy",  "kWh", true,    vtReal,      100} },
   { "H18",       { 39, "Amount Charged Energy",     "kWh", true,    vtReal,      100} },
   { "H19",       { 40, "Total kWh",                 "kWh", true,    vtReal,      100} },
   { "H20",       { 41, "Today kWh",                 "kWh", true,    vtReal,      100} },
   { "H21",       { 42, "Max Power Today",           "W",   true,    vtInt,       0} },
   { "H22",       { 43, "Yesterday kWh",             "kWh", true,    vtReal,      100} },
   { "H23",       { 44, "Max Power Yesterday",       "W",   true,    vtInt,       0} },
   { "ERR",       { 45, "Error",                     "",    true,    vtInt,       0} },
   { "CS",        { 46, "State",                     "",    true,    vtInt,       0} },
   { "BMV",       { 47, "Model",                     "",    true,    vtString,    0} },
   { "FW",        { 48, "Firmware Version 16",       "",    false,   vtString,    0} },
   { "FWE",       { 49, "Firmware Version 24",       "",    false,   vtString,    0} },
   { "PID",       { 50, "Device",                    "",    false,   vtString,    0} },
   { "SER#",      { 51, "Serialnumber",              "",    false,   vtString,    0} },
   { "HSDS",      { 52, "Day",                       "",    true,    vtInt,       0} },
   { "MODE",      { 53, "Device Mode",               "",    true,    vtString,    0} },
   { "AC_OUT_V",  { 54, "AC Out Volt",               "V",   true,    vtReal,      100} },
   { "AC_OUT_I",  { 55, "AC Out Current",            "A",   true,    vtReal,      10} },
   { "AC_OUT_S",  { 56, "AC Out Apparent Power",     "W",   true,    vtReal,      1} },
   { "WARN",      { 57, "Warning Reason",            "",    true,    vtString,    0} },
   { "MPPT",      { 58, "Tracker Operation Mode",    "",    true,    vtString,    0} },
   { "MON",       { 59, "DC Monitor Mode",           "",    true,    vtString,    0} },
   { "DC_IN_V",   { 60, "DC Input Voltage",          "V",   true,    vtReal,      100} },
   { "DC_IN_I",   { 61, "DC Input Current",          "A",   true,    vtReal,      10} },
   { "DC_IN_P",   { 62, "DC Input Power",            "W",   true,    vtInt,       0} },

   // internal definitions

   { "_INFO",  { vdraInfo, "Info",                   "",    true,    vtString,    0} },
   { "_MODE",  { vdraMode, "Device Mode Setting",    "",    true,    vtString,    0} }
};

// AR (alarm code) and WARN (warn reason)
// TODO: More than one bit can set!

static std::map<uint,std::string> veDirectDeviceCodeAR
{
    { 0,    "none" },
    { 1,    "Low Voltage" },
    { 2,    "High Voltage" },
    { 4,    "Low SOC" },
    { 8,    "Low Starter Voltage2" },
    { 16,   "High Starter Voltage" },
    { 32,   "Low Temperature" },
    { 64,   "High Temperature" },
    { 128,  "Mid Voltage" },
    { 256,  "Overload" },
    { 512,  "DC ripple" },
    { 1024, "Low V AC out" },
    { 2048, "LHigh V AC out" },
    { 4096, "Short Circuit" },
    { 8192, "BMS Lockout" }
};

// OR (off reason)

static std::map<uint,std::string> veDirectDeviceCodeOR
{
    { 0x000, "none" },
    { 0x001, "No input power" },
    { 0x002, "Switched off (hard)" },  // (power switch)
    { 0x004, "Switched off (soft)" },  // (device mode register)
    { 0x008, "Remote input" },
    { 0x010, "Protection active" },
    { 0x020, "Paygo" },
    { 0x040, "BMS" },
    { 0x080, "Engine shutdown detection" },
    { 0x100, "Analysing input voltage" },
};

// AP_BLE

static std::map<uint,std::string> veDirectDeviceCodeAP_BLE
{
   {0x01, "BLE supports switching off"},
   {0x02, "BLE switching off is permanent"}
};


// MODE (device mode)

static std::map<uint,std::string> veDirectDeviceCodeMODE
{
   { 1,   "Charger" },
   { 2,   "Inverter" },
   { 4,   "Off" },
   { 5,   "Eco" },
   { 253, "Hibernate" }
};

// CS (operation state)

static std::map<uint,std::string> veDirectDeviceCodeCS
{
    { 0,   "Off" },
    { 1,   "Low power" },
    { 2,   "Fault" },
    { 3,   "Bulk" },
    { 4,   "Absorption" },
    { 5,   "Float" },
    { 6,   "Storage" },
    { 7,   "Equalize (manual)" },
    { 9,   "Inverting" },
    { 11,  "Power supply" },
    { 245, "Starting-up" },
    { 246, "Repeated absorption" },
    { 247, "Auto equalize / Recondition" },
    { 248, "BatterySafe" },
    { 252, "External Control" }
};

// ERR (Current_error)

static std::map<uint,std::string> veDirectDeviceCodeERR
{
    { 0,   "No error" },
    { 2,   "Battery voltage too high" },
    { 17,  "Charger temperature too high" },
    { 18,  "Charger over current" },
    { 19,  "Charger current reversed" },
    { 20,  "Bulk time limit exceeded" },
    { 21,  "Current sensor issue (sensor bias/sensor broken)" },
    { 26,  "Terminals overheated" },
    { 28,  "Converter issue" },
    { 33,  "Input voltage too high (solar panel)" },
    { 34,  "Input current too high (solar panel)" },
    { 38,  "Input shutdown (excessive battery voltage)" },
    { 39,  "Input shutdown (current flow during off mode)" },
    { 65,  "Lost communication with one of devices" },
    { 66,  "Synchronised charging device configuration issue" },
    { 67,  "BMS connection lost" },
    { 68,  "Network misconfigured" },
    { 116, "Factory calibration data lost" },
    { 117, "Invalid/incompatible firmware" },
    { 119, "User settings invalid" }
};

// MPPT (Tracker_operation_mode)

static std::map<uint,std::string> veDirectDeviceCodeMPPT
{
   { 0, "Off"  },
   { 1, "Voltage or current limited" },
   { 2, "MPP Tracker active" }
};

// The product id: PID

static std::map<uint,std::string> veDirectDeviceList
{
   { 0x203,  "BMV-700" },
   { 0x204,  "BMV-702" },
   { 0x205,  "BMV-700H" },

   { 0x0300, "BlueSolar MPPT 70|15" },
   { 0xA040, "BlueSolar MPPT 75|50" },
   { 0xA041, "BlueSolar MPPT 150|35" },
   { 0xA042, "BlueSolar MPPT 75|15" },
   { 0xA043, "BlueSolar MPPT 100|15" },
   { 0xA044, "BlueSolar MPPT 100|30" },
   { 0xA045, "BlueSolar MPPT 100|50" },
   { 0xA046, "BlueSolar MPPT 150|70" },
   { 0xA047, "BlueSolar MPPT 150|100 " },
   { 0xA049, "BlueSolar MPPT 100|50 rev2" },
   { 0xA04A, "BlueSolar MPPT 100|30 rev2" },
   { 0xA04B, "BlueSolar MPPT 150|35" },
   { 0xA04C, "BlueSolar MPPT 75|10" },
   { 0xA04D, "BlueSolar MPPT 150|45" },
   { 0xA04E, "BlueSolar MPPT 150|60" },
   { 0xA04F, "BlueSolar MPPT 150|85" },
   { 0xA050, "SmartSolar MPPT 250|100" },
   { 0xA051, "SmartSolar MPPT 150|100" },
   { 0xA052, "SmartSolar MPPT 150|85" },
   { 0xA053, "SmartSolar MPPT 75|15" },
   { 0xA054, "SmartSolar MPPT 75|10" },
   { 0xA055, "SmartSolar MPPT 100|15" },
   { 0xA056, "SmartSolar MPPT 100|30" },
   { 0xA057, "SmartSolar MPPT 100|50" },
   { 0xA058, "SmartSolar MPPT 150|35" },
   { 0xA059, "SmartSolar MPPT 150|100 rev2" },
   { 0xA05A, "SmartSolar MPPT 150|85 rev2" },
   { 0xA05B, "SmartSolar MPPT 250|70" },
   { 0xA05C, "SmartSolar MPPT 250|85" },
   { 0xA05D, "SmartSolar MPPT 250|60" },
   { 0xA05E, "SmartSolar MPPT 250|45" },
   { 0xA05F, "SmartSolar MPPT 100|20" },
   { 0xA060, "SmartSolar MPPT 100|20 48V" },
   { 0xA061, "SmartSolar MPPT 150|45" },
   { 0xA062, "SmartSolar MPPT 150|60" },
   { 0xA063, "SmartSolar MPPT 150|70" },
   { 0xA064, "SmartSolar MPPT 250|85 rev2" },
   { 0xA065, "SmartSolar MPPT 250|100 rev2" },
   { 0xA066, "BlueSolar MPPT 100|20" },
   { 0xA067, "BlueSolar MPPT 100|20 48V" },
   { 0xA068, "SmartSolar MPPT 250|60 rev2" },
   { 0xA069, "SmartSolar MPPT 250|70 rev2" },
   { 0xA06A, "SmartSolar MPPT 150|45 rev2" },
   { 0xA06B, "SmartSolar MPPT 150|60 rev2" },
   { 0xA06C, "SmartSolar MPPT 150|70 rev2" },
   { 0xA06D, "SmartSolar MPPT 150|85 rev3" },
   { 0xA06E, "SmartSolar MPPT 150|100 rev3" },
   { 0xA06F, "BlueSolar MPPT 150|45 rev2" },
   { 0xA070, "BlueSolar MPPT 150|60 rev2" },
   { 0xA071, "BlueSolar MPPT 150|70 rev2" },
   { 0xA072, "BlueSolar MPPT 150/45 rev3" },
   { 0xA073, "SmartSolar MPPT 150/45 rev3" },
   { 0xA074, "SmartSolar MPPT 75/10 rev2" },
   { 0xA075, "SmartSolar MPPT 75/15 rev2" },
   { 0xA076, "BlueSolar MPPT 100/30 rev3" },
   { 0xA077, "BlueSolar MPPT 100/50 rev3" },
   { 0xA078, "BlueSolar MPPT 150/35 rev3" },
   { 0xA079, "BlueSolar MPPT 75/10 rev2" },
   { 0xA07A, "BlueSolar MPPT 75/15 rev2" },
   { 0xA07B, "BlueSolar MPPT 100/15 rev2" },
   { 0xA07C, "BlueSolar MPPT 75/10 rev3" },
   { 0xA07D, "BlueSolar MPPT 75/15 rev3" },
   { 0xA07E, "SmartSolar MPPT 100/30 12V" },
   { 0xA07F, "All-In-1 SmartSolar MPPT 75/15 12V" },
   { 0xA102, "SmartSolar MPPT VE.Can 150/70" },
   { 0xA103, "SmartSolar MPPT VE.Can 150/45" },
   { 0xA104, "SmartSolar MPPT VE.Can 150/60" },
   { 0xA105, "SmartSolar MPPT VE.Can 150/85" },
   { 0xA106, "SmartSolar MPPT VE.Can 150/100" },
   { 0xA107, "SmartSolar MPPT VE.Can 250/45" },
   { 0xA108, "SmartSolar MPPT VE.Can 250/60" },
   { 0xA109, "SmartSolar MPPT VE.Can 250/70" },
   { 0xA10A, "SmartSolar MPPT VE.Can 250/85" },
   { 0xA10B, "SmartSolar MPPT VE.Can 250/100" },
   { 0xA10C, "SmartSolar MPPT VE.Can 150/70 rev2" },
   { 0xA10D, "SmartSolar MPPT VE.Can 150/85 rev2" },
   { 0xA10E, "SmartSolar MPPT VE.Can 150/100 rev2" },
   { 0xA10F, "BlueSolar MPPT VE.Can 150/100" },
   { 0xA112, "BlueSolar MPPT VE.Can 250/70" },
   { 0xA113, "BlueSolar MPPT VE.Can 250/100" },
   { 0xA114, "SmartSolar MPPT VE.Can 250/70 rev2" },
   { 0xA115, "SmartSolar MPPT VE.Can 250/100 rev2" },
   { 0xA116, "SmartSolar MPPT VE.Can 250/85 rev2" },
   { 0xA117, "BlueSolar MPPT VE.Can 150/100 rev2" },

   { 0xA201, "Phoenix Inverter 12V 250VA 230V" },
   { 0xA202, "Phoenix Inverter 24V 250VA 230V" },
   { 0xA204, "Phoenix Inverter 48V 250VA 230V" },
   { 0xA211, "Phoenix Inverter 12V 375VA 230V" },
   { 0xA212, "Phoenix Inverter 24V 375VA 230V" },
   { 0xA214, "Phoenix Inverter 48V 375VA 230V" },
   { 0xA221, "Phoenix Inverter 12V 500VA 230V" },
   { 0xA222, "Phoenix Inverter 24V 500VA 230V" },
   { 0xA224, "Phoenix Inverter 48V 500VA 230V" },
   { 0xA231, "Phoenix Inverter 12V 250VA 230V" },
   { 0xA232, "Phoenix Inverter 24V 250VA 230V" },
   { 0xA234, "Phoenix Inverter 48V 250VA 230V" },
   { 0xA239, "Phoenix Inverter 12V 250VA 120V" },
   { 0xA23A, "Phoenix Inverter 24V 250VA 120V" },
   { 0xA23C, "Phoenix Inverter 48V 250VA 120V" },
   { 0xA241, "Phoenix Inverter 12V 375VA 230V" },
   { 0xA242, "Phoenix Inverter 24V 375VA 230V" },
   { 0xA244, "Phoenix Inverter 48V 375VA 230V" },
   { 0xA249, "Phoenix Inverter 12V 375VA 120V" },
   { 0xA24A, "Phoenix Inverter 24V 375VA 120V" },
   { 0xA24C, "Phoenix Inverter 48V 375VA 120V" },
   { 0xA251, "Phoenix Inverter 12V 500VA 230V" },
   { 0xA252, "Phoenix Inverter 24V 500VA 230V" },
   { 0xA254, "Phoenix Inverter 48V 500VA 230V" },
   { 0xA259, "Phoenix Inverter 12V 500VA 120V" },
   { 0xA25A, "Phoenix Inverter 24V 500VA 120V" },
   { 0xA25C, "Phoenix Inverter 48V 500VA 120V" },
   { 0xA261, "Phoenix Inverter 12V 800VA 230V" },
   { 0xA2E1, "Phoenix Inverter 12V 800VA 230V HS redesign" },
   { 0xA262, "Phoenix Inverter 24V 800VA 230V" },
   { 0xA2E2, "Phoenix Inverter 24V 800VA 230V HS redesign" },
   { 0xA264, "Phoenix Inverter 48V 800VA 230V" },
   { 0xA2E4, "Phoenix Inverter 48V 800VA 230V HS redesign" },
   { 0xA269, "Phoenix Inverter 12V 800VA 120V" },
   { 0xA2E9, "Phoenix Inverter 12V 800VA 120V HS redesign" },
   { 0xA26A, "Phoenix Inverter 24V 800VA 120V" },
   { 0xA2EA, "Phoenix Inverter 24V 800VA 120V HS redesign" },
   { 0xA26C, "Phoenix Inverter 48V 800VA 120V" },
   { 0xA2EC, "Phoenix Inverter 48V 800VA 120V HS redesign" },
   { 0xA271, "Phoenix Inverter 12V 1200VA 230V" },
   { 0xA2F1, "Phoenix Inverter 12V 1200VA 230V HS redesign" },
   { 0xA272, "Phoenix Inverter 24V 1200VA 230V" },
   { 0xA274, "Phoenix Inverter 48V 1200VA 230V" },
   { 0xA279, "Phoenix Inverter 12V 1200VA 120V" },
   { 0xA27A, "Phoenix Inverter 24V 1200VA 120V" },
   { 0xA27C, "Phoenix Inverter 48V 1200VA 120V" },
   { 0xA281, "Phoenix Inverter 12V 1600VA 230V" },
   { 0xA282, "Phoenix Inverter 24V 1600VA 230V" },
   { 0xA284, "Phoenix Inverter 48V 1600VA 230V" },
   { 0xA291, "Phoenix Inverter 12V 2000VA 230V" },
   { 0xA292, "Phoenix Inverter 24V 2000VA 230V" },
   { 0xA294, "Phoenix Inverter 48V 2000VA 230V" },
   { 0xA2A1, "Phoenix Inverter 12V 3000VA 230V" },
   { 0xA2A2, "Phoenix Inverter 24V 3000VA 230V" },
   { 0xA2A4, "Phoenix Inverter 48V 3000VA 230V" },
   { 0xA2B1, "Phoenix Inverter Smart 12V 5000VA 230VA Smart, Bluetooth" },
   { 0xA2B2, "Phoenix Inverter Smart 24V 5000VA 230VA Smart, Bluetooth" },
   { 0xA2B4, "Phoenix Inverter Smart 48V 5000VA 230VA Smart, Bluetooth" },

   { 0xA340, "Phoenix Smart IP43 Charger 12|50 (1+1)" },
   { 0xA341, "Phoenix Smart IP43 Charger 12|50 (3)" },
   { 0xA342, "Phoenix Smart IP43 Charger 24|25 (1+1)" },
   { 0xA343, "Phoenix Smart IP43 Charger 24|25 (3)" },
   { 0xA344, "Phoenix Smart IP43 Charger 12|30 (1+1)" },
   { 0xA345, "Phoenix Smart IP43 Charger 12|30 (3)" },
   { 0xA346, "Phoenix Smart IP43 Charger 24|16 (1+1)" },
   { 0xA347, "Phoenix Smart IP43 Charger 24|16 (3)" },
   { 0xA381, "BMV-712 Smart" },
   { 0xA382, "BMV-710H Smart" },
   { 0xA383, "BMV-712 Smart Rev2" },
   { 0xA389, "SmartShunt 500A/50mV" },
   { 0xA38A, "SmartShunt 1000A/50mV" },
   { 0xA38B, "SmartShunt 2000A/50mV" },
   { 0xA3F0, "Smart BuckBoost 12V/12V-50A" },

   { 0x9999, "The migthy simluator" }  // data for the Ve Simulator
};
