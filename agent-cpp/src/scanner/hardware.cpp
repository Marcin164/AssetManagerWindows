#include "lanventory/scanner.hpp"
#include "lanventory/wmi.hpp"

namespace lv::scanner {
namespace {

// Lowercase + snake_case keys the frontend reads (CPU.tsx, RAM.tsx, ...)
// don't match WMI's CamelCase, so we project each row.
nlohmann::json remap(const nlohmann::json& row,
                     std::initializer_list<std::pair<const char*, const char*>> pairs) {
    nlohmann::json out = nlohmann::json::object();
    for (auto& [from, to] : pairs) {
        if (row.contains(from)) out[to] = row[from];
    }
    return out;
}

nlohmann::json projection_array(
    std::wstring_view wql,
    std::initializer_list<std::pair<const char*, const char*>> pairs) {
    auto rows = wmi::query(wql);
    nlohmann::json out = nlohmann::json::array();
    for (auto& r : rows) out.push_back(remap(r, pairs));
    return out;
}

}  // namespace

nlohmann::json collect_hardware() {
    nlohmann::json out;

    out["baseboard"] = remap(
        wmi::query_first(
            L"SELECT SerialNumber, Manufacturer, Product, HostingBoard, Version "
            L"FROM Win32_BaseBoard"),
        {{"SerialNumber", "serial_number"},
         {"Manufacturer", "manufacturer"},
         {"Product",      "product"},
         {"HostingBoard", "hosting_board"},
         {"Version",      "version"}});

    out["cpu"] = projection_array(
        L"SELECT Name, ProcessorId, NumberOfCores, NumberOfLogicalProcessors, "
        L"Architecture, L2CacheSize, L3CacheSize, SocketDesignation, "
        L"CurrentClockSpeed, MaxClockSpeed FROM Win32_Processor",
        {{"Name",                      "name"},
         {"ProcessorId",                "processor_id"},
         {"NumberOfCores",              "cores"},
         {"NumberOfLogicalProcessors",  "threads"},
         {"Architecture",               "architecture"},
         {"L2CacheSize",                "l2_cache"},
         {"L3CacheSize",                "l3_cache"},
         {"SocketDesignation",          "socket"},
         {"CurrentClockSpeed",          "current_clock_speed"},
         {"MaxClockSpeed",              "max_clock_speed"}});

    // RAM capacity is uint64 reported in bytes; frontend stringifies it.
    auto ram_rows = wmi::query(
        L"SELECT Manufacturer, PartNumber, SerialNumber, Speed, Capacity, "
        L"BankLabel, DeviceLocator FROM Win32_PhysicalMemory");
    nlohmann::json rams = nlohmann::json::array();
    for (auto& r : ram_rows) {
        nlohmann::json m = remap(r,
            {{"Manufacturer",  "manufacturer"},
             {"PartNumber",    "part_number"},
             {"SerialNumber",  "serial_number"},
             {"Speed",         "speed"},
             {"BankLabel",     "bank_label"},
             {"DeviceLocator", "device_locator"}});
        if (r.contains("Capacity")) {
            // Stringify so Number.parseInt() on the frontend matches the
            // 64-bit value without precision loss.
            if (r["Capacity"].is_number()) {
                m["capacity"] = std::to_string(r["Capacity"].get<long long>());
            } else if (r["Capacity"].is_string()) {
                m["capacity"] = r["Capacity"];
            }
        }
        rams.push_back(m);
    }
    out["ram_modules"] = rams;

    // GPUs -- frontend builds "WxH" string from horizontal/vertical.
    auto gpu_rows = wmi::query(
        L"SELECT Name, AdapterRAM, MaxRefreshRate, "
        L"CurrentHorizontalResolution, CurrentVerticalResolution, "
        L"VideoProcessor, DriverVersion FROM Win32_VideoController");
    nlohmann::json gpus = nlohmann::json::array();
    for (auto& g : gpu_rows) {
        nlohmann::json m = remap(g,
            {{"Name",            "name"},
             {"AdapterRAM",      "adapter_ram"},
             {"MaxRefreshRate",  "max_refresh_rate"},
             {"VideoProcessor",  "video_processor"},
             {"DriverVersion",   "driver_version"}});
        long h = g.value("CurrentHorizontalResolution", 0);
        long v = g.value("CurrentVerticalResolution", 0);
        m["current_resolution"] = std::to_string(h) + "x" + std::to_string(v);
        gpus.push_back(m);
    }
    out["gpus"] = gpus;

    out["bios"] = remap(
        wmi::query_first(
            L"SELECT Manufacturer, SerialNumber, SMBIOSMajorVersion, "
            L"SMBIOSMinorVersion, SMBIOSBIOSVersion FROM Win32_BIOS"),
        {{"Manufacturer",       "manufacturer"},
         {"SerialNumber",       "serial_number"},
         {"SMBIOSMajorVersion", "smbios_major"},
         {"SMBIOSMinorVersion", "smbios_minor"},
         {"SMBIOSBIOSVersion",  "version"}});

    // Disks + their logical-disk partitions. WMI associators are
    // painful to navigate procedurally, so we cross-reference by
    // DeviceID through Win32_DiskDriveToDiskPartition and
    // Win32_LogicalDiskToPartition.
    auto disks = wmi::query(
        L"SELECT DeviceID, Model, SerialNumber FROM Win32_DiskDrive");
    auto logical_disks = wmi::query(
        L"SELECT DeviceID, FileSystem, FreeSpace, Size, VolumeName "
        L"FROM Win32_LogicalDisk");

    // Build a flat list of all partitions per logical disk -- WMI's
    // associator chain is heavy and brittle. Frontend Disks.tsx only
    // really cares about the model + serial + a partitions array, so
    // we emit one synthetic disk row carrying all logical disks. This
    // matches the visual layout: "Disk X" then partition bars under it.
    nlohmann::json disk_arr = nlohmann::json::array();
    for (auto& d : disks) {
        nlohmann::json entry = {
            {"model",         d.value("Model", "")},
            {"serial_number", d.value("SerialNumber", "")},
            {"partitions",    nlohmann::json::array()},
        };
        disk_arr.push_back(entry);
    }
    if (!disk_arr.empty()) {
        for (auto& l : logical_disks) {
            long long size = 0, free_space = 0;
            if (l.contains("Size") && l["Size"].is_number())
                size = l["Size"].get<long long>();
            if (l.contains("FreeSpace") && l["FreeSpace"].is_number())
                free_space = l["FreeSpace"].get<long long>();
            nlohmann::json p = {
                {"device_id",   l.value("DeviceID", "")},
                {"file_system", l.value("FileSystem", "")},
                {"free_space",  free_space},
                {"total_size",  size},
                {"used_space",  size - free_space},
                {"volume_name", l.value("VolumeName", "")},
            };
            disk_arr.front()["partitions"].push_back(p);
        }
    }
    out["disks"] = disk_arr;

    return out;
}

}  // namespace lv::scanner
