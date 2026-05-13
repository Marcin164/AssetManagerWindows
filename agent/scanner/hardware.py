"""Hardware section -- matches CPU/RAM/MOBO/Disks/GPU/BIOS.tsx."""

from __future__ import annotations

from typing import Any

from .helpers import as_list, run_powershell, safe


_BASEBOARD_PS = r"""
$b = Get-CimInstance Win32_BaseBoard | Select-Object -First 1
if ($b) {
  [pscustomobject]@{
    serial_number = $b.SerialNumber; manufacturer = $b.Manufacturer
    product = $b.Product; hosting_board = $b.HostingBoard; version = $b.Version
  }
}
"""

_CPU_PS = r"""
Get-CimInstance Win32_Processor | ForEach-Object {
  [pscustomobject]@{
    name = $_.Name; processor_id = $_.ProcessorId
    cores = $_.NumberOfCores; threads = $_.NumberOfLogicalProcessors
    architecture = $_.Architecture; l2_cache = $_.L2CacheSize; l3_cache = $_.L3CacheSize
    socket = $_.SocketDesignation
    current_clock_speed = $_.CurrentClockSpeed; max_clock_speed = $_.MaxClockSpeed
  }
}
"""

_RAM_PS = r"""
Get-CimInstance Win32_PhysicalMemory | ForEach-Object {
  [pscustomobject]@{
    manufacturer = $_.Manufacturer; part_number = ($_.PartNumber -replace '\s+$','')
    serial_number = $_.SerialNumber; speed = $_.Speed; capacity = [string]$_.Capacity
    bank_label = $_.BankLabel; device_locator = $_.DeviceLocator
  }
}
"""

_GPU_PS = r"""
Get-CimInstance Win32_VideoController | ForEach-Object {
  [pscustomobject]@{
    name = $_.Name; adapter_ram = $_.AdapterRAM; max_refresh_rate = $_.MaxRefreshRate
    current_resolution = "$($_.CurrentHorizontalResolution)x$($_.CurrentVerticalResolution)"
    video_processor = $_.VideoProcessor; driver_version = $_.DriverVersion
  }
}
"""

_BIOS_PS = r"""
$b = Get-CimInstance Win32_BIOS | Select-Object -First 1
if ($b) {
  [pscustomobject]@{
    manufacturer = $b.Manufacturer; serial_number = $b.SerialNumber
    smbios_major = $b.SMBIOSMajorVersion; smbios_minor = $b.SMBIOSMinorVersion
    version = $b.SMBIOSBIOSVersion
  }
}
"""

_DISKS_PS = r"""
$disks = Get-CimInstance Win32_DiskDrive
foreach ($d in $disks) {
  $parts = Get-CimAssociatedInstance -InputObject $d -ResultClassName Win32_DiskPartition
  $partitionsOut = foreach ($p in $parts) {
    $logical = Get-CimAssociatedInstance -InputObject $p -ResultClassName Win32_LogicalDisk
    foreach ($l in $logical) {
      [pscustomobject]@{
        device_id = $l.DeviceID; file_system = $l.FileSystem
        free_space = [int64]$l.FreeSpace; total_size = [int64]$l.Size
        used_space = [int64]($l.Size - $l.FreeSpace); volume_name = $l.VolumeName
      }
    }
  }
  [pscustomobject]@{
    model = $d.Model
    serial_number = ($d.SerialNumber -replace '\s+$','')
    partitions = @($partitionsOut)
  }
}
"""


def collect_hardware() -> dict[str, Any]:
    return {
        "baseboard":   safe(run_powershell, None, _BASEBOARD_PS) or {},
        "cpu":         as_list(safe(run_powershell, [], _CPU_PS)),
        "ram_modules": as_list(safe(run_powershell, [], _RAM_PS)),
        "gpus":        as_list(safe(run_powershell, [], _GPU_PS)),
        "bios":        safe(run_powershell, None, _BIOS_PS) or {},
        "disks":       as_list(safe(run_powershell, [], _DISKS_PS)),
    }
