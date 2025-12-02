# Changelog

All notable changes to PlantOS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.3] - 2025-12-02

### Changed
- Modified `task run` to default to serial/USB flashing without user prompt
- Added OTA flashing option via `task run OTA=true` command
- Serial device auto-detection now matches behavior from `snoop` task
- Eliminated interactive flash method selection prompt

## [0.3.2] - Previous Release

### Added
- WDT Manager with user-based subscription
- PersistentStateManager Component and PSMChecker Test Component
- ActuatorSafetyGate with DummyActuatorTrigger

### Fixed
- Web server working in mesh network again
- Disabled WDT spam for cleaner logs
