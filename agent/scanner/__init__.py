"""Per-section scanners. Field names match
``frontend/src/Pages/Main/Devices``."""

from .system import collect_system
from .hardware import collect_hardware
from .software import collect_software
from .network import collect_network
from .security import collect_security
from .peripherals import collect_peripherals
from .events import collect_events
from .users import collect_users

__all__ = [
    "collect_system", "collect_hardware", "collect_software", "collect_network",
    "collect_security", "collect_peripherals", "collect_events", "collect_users",
]
