# SdHostController

## Overview

Drive that implements `if_OS_Storage`, and allows accessing the SD card
peripheral.

### Implementation

This component will automatically initiate the SD card hardware peripheral
during the initialization phase, so that the card can be accessed via the
blocking RPC calls with data being exchanged via the dedicated data port.

Please note that driver currently assumes that SD card is inserted during the
entire power cycle, and does not support SD card removal/insertion events!

## Usage

This is how the component can be instantiated in the system.

### Declaration of the component in CMake

For declaring this component in CMake, pass only the name of the component as
below:

```C
DeclareCAmkESComponent_SdHostController(
    <NameOfTheComponent>
)
```

## Instantiation and configuration in CAmkES

The component requires only the connection with the client, and the hardware
configuration to be set properly.

### Declaring the component

```C
#include "SdHostController/SdHostController.camkes"
DECLARE_COMPONENT_SDHC(<NameOfTheComponent>);
```

### Instantiating and connecting the component

```C
DECLARE_AND_CONNECT_INSTANCE_SDHC(<NameOfComponent>, <NameOfInstance>)

connection seL4RPCCall <client_sdhc_interface>(
                            from <Client>.<rpc>,
                            to <NameOfInstance>.<rpc>);

connection seL4SharedData <client_sdhc_dataport>(
                            from <Client>.<dataport>,
                            to <NameOfInstance>.<dataport>);
```

### Configuring the instance

Please note, that the desired peripheral's port must be specified here.

```C
CONFIGURE_INSTANCE_SDHC(<NameOfInstance>, <portId>)
```

## Example

In the following example, we instantiate the SdHostController for the default
peripheral address and IRQ.

### Instantiation of the component in CMake

`Sdhc` has been chosen as the component's name:

```C
DeclareCAmkESComponent_SdHostController(
    Sdhc
)
```

### Instantiation and configuration of the component  in CAmkES

In the main CAmkES composition we instantiate the SdHostController, connect
it to its single client, and configure the peripheral's parameters correctly.

#### Declaring the component in the main CAmkES file

Here the `Sdhc` has been used as the component's type, following what was
specified in the CMake file.

```C
#include "SdHostController/SdHostController.camkes"
DECLARE_COMPONENT_SDHC(Sdhc);
```

### Instantiating and connecting the component in the main CAmkES file

`sdhc` has been chosen as the instance name, and the component is connected to
the `fileManager` component.

```C
DECLARE_AND_CONNECT_INSTANCE_SDHC(Sdhc, sdhc)

connection seL4RPCCall fileManager_sdhc_rpc(
                            from fileManager.storage_rpc,
                            to   sdhc.storage_rpc);

connection seL4SharedData fileManager_sdhc_rpc_port(
                            from fileManager.storage_port,
                            to   sdhc.storage_port);
```

### Configuring the instance in the main CAmkES file

Last but not least, the desired peripheral's port must be properly configured
(SDHC4 in this case):

```C
    CONFIGURE_INSTANCE_SDHC(sdhc, 4)
```

### Using the component's interfaces in C

`if_OS_Storage` is used for accessing directly the SD card.

Please note that only multiple of block size data chunks can be written or read
from the medium, and offset must also of a block size multiple.

```C
void writeAndReadBlocks(size_t blockSize, size_t nBlocks);

void writeAndRead()
{
    const size_t    blockSz         = sdhc_rpc_getBlockSize();
    const size_t    nBlocks         = 3;
    const size_t    dataSz          = blockSz * nBlocks;
    const off_t     desiredOffset   = 1 * blockSize;
    const uint8_t   writePattern    = 0xA5;

    // Verify in the production code that there is no data port overflow.
    memset(sdhc_storage_port, writePattern, dataSz);

    size_t bytesWritten = 0U;
    assert(OS_SUCCESS == storage_rpc_write(
                            desiredOffset,
                            dataSz,
                            &bytesWritten));

    assert(dataSz == bytesWritten);

    // Clearing the data port for sanity.
    memset(sdhc_storage_port, 0xFF, dataSz);

    size_t bytesRead = 0U;
    assert(OS_SUCCESS == storage_rpc_read(
                            desiredOffset,
                            dataSz,
                            &bytesRead));

    assert(dataSz == bytesRead);

    // Verifying the read content:
    for(size_t i; i < dataSz; ++i)
    {
        assert(writePattern == sdhc_storage_port[i]);
    }
}
```
