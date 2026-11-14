# Roadmap

Core controller operation comes first: a solid connection, an accurate heading, click or type to turn, stop, and reconnect. 0.1.0-alpha4 keeps the 3-second connect limit from alpha3, starts a new rotor with no address filled in, and adds **Find RT-21** (USB and COM ports first, network only when asked). `meridian.ini`, `logs/`, and `backups/` sit next to the program unless that folder is read-only.

Replacing the Green Heron RT-21 Setup Utility (4.B12) is the next product step. The inventory, risk, and Controller-tab design are in [RT21_SETUP_PARITY.md](RT21_SETUP_PARITY.md). Writes stay compiled out until Andrew has approved each group on the real unit: read everything, back it up, change one harmless setting, read it back, and restore. The network module's own Ethernet and Wi-Fi settings are the priority inside that work, because the Green Heron utility routinely crashes there.
