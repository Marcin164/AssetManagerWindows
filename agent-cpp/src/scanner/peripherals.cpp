#include "lanventory/scanner.hpp"
#include "lanventory/wmi.hpp"

namespace lv::scanner {
namespace {

nlohmann::json project(const nlohmann::json& rows,
                       std::initializer_list<std::pair<const char*, const char*>> pairs) {
    nlohmann::json out = nlohmann::json::array();
    for (auto& r : rows) {
        nlohmann::json m = nlohmann::json::object();
        for (auto& [from, to] : pairs) {
            if (r.contains(from)) m[to] = r[from];
        }
        out.push_back(m);
    }
    return out;
}

}  // namespace

nlohmann::json collect_peripherals() {
    nlohmann::json out;

    out["mice"] = project(wmi::query(
        L"SELECT Name, Manufacturer, PointingType, PNPDeviceID, Status, "
        L"NumberOfButtons FROM Win32_PointingDevice"),
        {{"Name",            "name"},
         {"Manufacturer",    "manufacturer"},
         {"PointingType",    "pointing_type"},
         {"PNPDeviceID",     "pnp_device_id"},
         {"Status",          "status"},
         {"NumberOfButtons", "buttons"}});

    out["keyboards"] = project(wmi::query(
        L"SELECT Name, Description, NumberOfFunctionKeys, PNPDeviceID, "
        L"Status, Layout FROM Win32_Keyboard"),
        {{"Name",                 "name"},
         {"Description",          "manufacturer"},
         {"NumberOfFunctionKeys", "function_keys"},
         {"PNPDeviceID",          "pnp_device_id"},
         {"Status",               "status"},
         {"Layout",               "layout"}});

    out["monitors"] = project(wmi::query(
        L"SELECT Name, MonitorManufacturer, ScreenWidth, ScreenHeight, "
        L"PNPDeviceID, Status FROM Win32_DesktopMonitor"),
        {{"Name",                "name"},
         {"MonitorManufacturer", "manufacturer"},
         {"ScreenWidth",         "screen_width"},
         {"ScreenHeight",        "screen_height"},
         {"PNPDeviceID",         "pnp_device_id"},
         {"Status",              "status"}});

    out["sound_devices"] = project(wmi::query(
        L"SELECT Name, Manufacturer, PNPDeviceID, Status FROM Win32_SoundDevice"),
        {{"Name",         "name"},
         {"Manufacturer", "manufacturer"},
         {"PNPDeviceID",  "pnp_device_id"},
         {"Status",       "status"}});

    // Printers: a few booleans need explicit projection.
    auto printer_rows = wmi::query(
        L"SELECT Name, PortName, PNPDeviceID, DriverName, WorkOffline, "
        L"Default, Shared, PrinterStatus FROM Win32_Printer");
    nlohmann::json printers = nlohmann::json::array();
    for (auto& p : printer_rows) {
        nlohmann::json row = {
            {"name",          p.value("Name", "")},
            {"port",          p.value("PortName", "")},
            {"pnp_device_id", p.value("PNPDeviceID", "")},
            {"driver",        p.value("DriverName", "")},
            {"work_offline",  p.value("WorkOffline", false)},
            {"default",       p.value("Default", false)},
            {"shared",        p.value("Shared", false)},
            {"status",        std::to_string(p.value("PrinterStatus", 0L))},
        };
        printers.push_back(row);
    }
    out["printers"] = printers;

    // USB device descriptions -- a single flat string list is what the
    // frontend (USBDevices.tsx) renders.
    auto usbs = wmi::query(
        L"SELECT Description FROM Win32_PnPEntity "
        L"WHERE PNPDeviceID LIKE 'USB%'");
    nlohmann::json usb_list = nlohmann::json::array();
    for (auto& u : usbs) {
        if (u.contains("Description") && u["Description"].is_string()) {
            usb_list.push_back(u["Description"]);
        }
    }
    out["usb_devices"] = usb_list;

    // External drives = removable (DriveType=2) or network (DriveType=4).
    auto drives = wmi::query(
        L"SELECT DeviceID, VolumeName, FileSystem, FreeSpace, Size "
        L"FROM Win32_LogicalDisk WHERE DriveType=2 OR DriveType=4");
    nlohmann::json ext = nlohmann::json::array();
    for (auto& d : drives) {
        ext.push_back({
            {"DeviceID",   d.value("DeviceID", "")},
            {"VolumeName", d.value("VolumeName", "")},
            {"FileSystem", d.value("FileSystem", "")},
            {"FreeSpace",  d.value("FreeSpace", 0LL)},
            {"Size",       d.value("Size", 0LL)},
        });
    }
    out["external_drives"] = ext;

    return out;
}

}  // namespace lv::scanner
