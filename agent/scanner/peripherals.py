"""Peripherals section -- matches Mice/Keyboards/Monitors/Sound/Printers/USB/ExtDrives.tsx."""

from __future__ import annotations

from typing import Any

from .helpers import as_list, run_powershell, safe


_MICE_PS = r"""
Get-CimInstance Win32_PointingDevice | ForEach-Object {
  [pscustomobject]@{
    name = $_.Name; manufacturer = $_.Manufacturer
    pointing_type = [int]$_.PointingType; pnp_device_id = $_.PNPDeviceID
    status = $_.Status; buttons = [int]$_.NumberOfButtons
  }
}
"""

_KEYBOARDS_PS = r"""
Get-CimInstance Win32_Keyboard | ForEach-Object {
  [pscustomobject]@{
    name = $_.Name; manufacturer = $_.Description
    function_keys = [int]$_.NumberOfFunctionKeys; pnp_device_id = $_.PNPDeviceID
    status = $_.Status; layout = $_.Layout
  }
}
"""

_MONITORS_PS = r"""
Get-CimInstance Win32_DesktopMonitor | ForEach-Object {
  [pscustomobject]@{
    name = $_.Name; manufacturer = $_.MonitorManufacturer
    screen_width = $_.ScreenWidth; screen_height = $_.ScreenHeight
    pnp_device_id = $_.PNPDeviceID; status = $_.Status
  }
}
"""

_SOUND_PS = r"""
Get-CimInstance Win32_SoundDevice | ForEach-Object {
  [pscustomobject]@{
    name = $_.Name; manufacturer = $_.Manufacturer
    pnp_device_id = $_.PNPDeviceID; status = $_.Status
  }
}
"""

_PRINTERS_PS = r"""
Get-CimInstance Win32_Printer | ForEach-Object {
  [pscustomobject]@{
    name = $_.Name; port = $_.PortName; pnp_device_id = $_.PNPDeviceID
    driver = $_.DriverName; work_offline = [bool]$_.WorkOffline
    default = [bool]$_.Default; shared = [bool]$_.Shared
    status = "$($_.PrinterStatus)"
  }
}
"""

_USB_PS = r"""
Get-CimInstance Win32_USBControllerDevice | ForEach-Object {
  $dep = [wmi]$_.Dependent
  $dep.Description
} | Sort-Object -Unique
"""

_EXT_DRIVES_PS = r"""
Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=2 OR DriveType=4' |
  ForEach-Object {
    [pscustomobject]@{
      DeviceID = $_.DeviceID; VolumeName = $_.VolumeName
      FileSystem = $_.FileSystem
      FreeSpace = [int64]$_.FreeSpace; Size = [int64]$_.Size
    }
  }
"""


def collect_peripherals() -> dict[str, Any]:
    return {
        "mice":            as_list(safe(run_powershell, [], _MICE_PS)),
        "keyboards":       as_list(safe(run_powershell, [], _KEYBOARDS_PS)),
        "monitors":        as_list(safe(run_powershell, [], _MONITORS_PS)),
        "sound_devices":   as_list(safe(run_powershell, [], _SOUND_PS)),
        "printers":        as_list(safe(run_powershell, [], _PRINTERS_PS)),
        "usb_devices":     as_list(safe(run_powershell, [], _USB_PS)),
        "external_drives": as_list(safe(run_powershell, [], _EXT_DRIVES_PS)),
    }
