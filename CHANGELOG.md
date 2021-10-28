# Changelog

All notable changes by HENSOLDT Cyber GmbH to this 3rd party module included in
the TRENTOS SDK will be documented in this file.

For more details it is recommended to compare the 3rd party module at hand with
the previous versions of the TRENTOS SDK or the baseline version.

## [1.3]

### Added

- Start development based on commit 3b5c4cdf9e1753c76d0c797cf70c90307714f94d of
<https://github.com/seL4/projects_libs>.
- Integrate `libsdhcdrivers` into this repository.
- Add RPi3 platform support.
- Add RPi4 platform support.

### Changed

- Extract platform specific logic.
- Refactor card initialization and identification.
- Use unified BCM283x mailbox interface.
- Clean up the code:
  - Use typedefs;
  - Cast SDHC registers to a structure instead of using macro offsets;
  - Add and improve debug output;
  - Hide malloc and free behind a macro.
