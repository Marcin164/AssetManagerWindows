"""Software section -- matches Software/Appx/Features tables."""

from __future__ import annotations

from typing import Any

from .helpers import as_list, run_powershell, safe


_INSTALLED_PS = r"""
$keys = @(
  'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
  'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
  'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*'
)
Get-ItemProperty $keys -ErrorAction SilentlyContinue |
  Where-Object { $_.DisplayName } |
  ForEach-Object {
    [pscustomobject]@{
      DisplayName    = $_.DisplayName
      DisplayVersion = $_.DisplayVersion
      Publisher      = $_.Publisher
      EstimatedSize  = $_.EstimatedSize
      InstallDate    = $_.InstallDate
    }
  } | Sort-Object DisplayName -Unique
"""

_APPX_PS = r"""
Get-AppxPackage -AllUsers -ErrorAction SilentlyContinue | ForEach-Object {
  [pscustomobject]@{
    Name = $_.Name; Publisher = $_.PackageFullName
    Version = $_.Version; Architecture = "$($_.Architecture)"
  }
}
"""

_FEATURES_PS = r"""
try {
  Get-WindowsOptionalFeature -Online -ErrorAction Stop | ForEach-Object {
    [pscustomobject]@{
      FeatureName = $_.FeatureName; State = "$($_.State)"
      Online = $_.Online; LogPath = $_.LogPath
    }
  }
} catch {}
"""


def collect_software() -> dict[str, Any]:
    return {
        "installed_programs": as_list(safe(run_powershell, [], _INSTALLED_PS, timeout=120)),
        "appx_packages":      as_list(safe(run_powershell, [], _APPX_PS, timeout=120)),
        "windows_features":   as_list(safe(run_powershell, [], _FEATURES_PS, timeout=180)),
    }
