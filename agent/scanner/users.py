"""Local users / groups / profiles -- matches LocalUsers/Groups/Profiles tables."""

from __future__ import annotations

from typing import Any

from .helpers import as_list, run_powershell, safe


_LOCAL_USERS_PS = r"""
Get-CimInstance Win32_UserAccount -Filter "LocalAccount=True" | ForEach-Object {
  [pscustomobject]@{
    Name = $_.Name; FullName = $_.FullName
    AccountType = [int]$_.AccountType; Disabled = [bool]$_.Disabled
    Lockout = [bool]$_.Lockout; PasswordChangeable = [bool]$_.PasswordChangeable
    PasswordExpires = [bool]$_.PasswordExpires; PasswordRequired = [bool]$_.PasswordRequired
    SID = $_.SID; Status = $_.Status
  }
}
"""

_LOCAL_GROUPS_PS = r"""
Get-CimInstance Win32_Group -Filter "LocalAccount=True" | ForEach-Object {
  [pscustomobject]@{
    Name = $_.Name; Description = $_.Description
    Status = $_.Status; SID = $_.SID
  }
}
"""

_USER_PROFILES_PS = r"""
Get-CimInstance Win32_UserProfile -ErrorAction SilentlyContinue | ForEach-Object {
  [pscustomobject]@{
    LocalPath = $_.LocalPath
    LastUseTime = if ($_.LastUseTime) { $_.LastUseTime.ToString('yyyy-MM-dd HH:mm:ss') } else { $null }
    HealthStatus = [int]$_.HealthStatus
    Loaded = [bool]$_.Loaded; Special = [bool]$_.Special
    SID = $_.SID; Status = $_.Status
  }
}
"""


def collect_users() -> dict[str, Any]:
    return {
        "local_users":    as_list(safe(run_powershell, [], _LOCAL_USERS_PS)),
        "local_groups":   as_list(safe(run_powershell, [], _LOCAL_GROUPS_PS)),
        "users_profiles": as_list(safe(run_powershell, [], _USER_PROFILES_PS)),
    }
