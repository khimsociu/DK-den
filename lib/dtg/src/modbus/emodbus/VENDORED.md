# Vendored eModbus RTU client

Upstream: https://github.com/eModbus/eModbus
Version: 1.7.4 (MIT, see license.md in this directory)
Scope: RTU client only. TCP, server, bridge and CoilData sources are not included,
so AsyncTCP is not required.

Only the RTU client subset is compiled, and all of it lives in the
`dtg_emodbus` namespace so a sketch can also install the original eModbus
library without duplicate symbols.

## SHA256 of the unmodified upstream files

```
9b1cd7723fcd4d6f975a197a7ddbdb6ceb4e7ea9a47cf14d44298873bc57f5da  ModbusClient.cpp
aebf0c701b99447f6ab0919449d50767311b73329fbff9fd9fe359f44ec01c78  ModbusClient.h
7d8fa179ce6ac39bcf99f5e12029dd191d31a623d78b37a7b534af1f997517ee  ModbusClientRTU.cpp
ea91e99562b296ce5695b2c856aa639d28f2e7ca27fcf2ffdcc33b084310f4bc  ModbusClientRTU.h
7e87e0a6f1e0688a174b899b18bcfd0f289e8f9dd48b998209f0e94c200fa3d6  ModbusError.h
dbcf7ce56a8cb84154723b66d77bb83387855d07f0936ce33d8e17e5b514bf42  ModbusMessage.cpp
422f5828f949806de86d91b38366f8e5a36d995405c7a8fb8b63cca80b11c3c8  ModbusMessage.h
777e05d7bb26fcb15b7ccaae207c36f825fe123216ad6ccc7488ca9c7e149b8f  ModbusTypeDefs.cpp
b4797bd17b3b5ad1df93b236d07ff49543a529e504a524fec615fcfc06ebca65  ModbusTypeDefs.h
471a9529091adc3038f22b0075e47ddd51a9062437428da17144166bad02c892  options.h
68e80eb7d5ee4e63fddc4ed07723217670b60751d3d7d0b08a3939e31d1a7522  RTUutils.cpp
24e1506aee85b4768df2f5498c414d39806dad909f78350837d0aea7ab17488e  RTUutils.h
36af83eb1ea31791d53f6bf5b520cf1b43f25aa35d809a801c5bd5dda460f995  Logging.cpp
3294d5b4efe2cf026deedacf28f761390e1929d8f310e9163b1634e0768ddad3  Logging.h
feaf5fae6eb266add8dcbbd51bef3ba4fd4180263dac2b0152470e8612cfc7b8  license.md
```

## Patches applied on top of 1.7.4

Search the sources for `DTG patch` to find each one.

- Namespace: every header and source is wrapped in `namespace dtg_emodbus`,
  include guards are prefixed `DTG_EMB_`, and internal includes are quoted.
  Exception: `FCT` is a class inside `namespace Modbus`, so its method
  definitions cannot take a namespace qualifier and would collide with a
  globally installed eModbus. It is renamed to `DtgFCT`. The RTU client never
  calls it.
- options.h: `LOG_LEVEL` defaults to `LOG_LEVEL_NONE`, so the worker never
  writes to Serial. `CLIENT_TASK_STACK` raised from 4096 to 5120 bytes.
- P1 (ModbusClientRTU::end): the worker is asked to stop and exits itself
  between transactions. `end()` waits at most one transaction timeout plus
  slack, and only force-kills the task if that deadline passes. A forced kill
  is reported through `wasForcedStop()`.
- P2: the request queue is only read and written while its mutex is held.
- P3: the worker blocks on a task notification instead of polling every 1 ms.
  Queueing a request notifies it.
- P4 (RTUutils::receive): the 512 byte receive buffer is a stack array instead
  of a per-frame `new[]`/`delete[]`.
- Worker task name changed from `ModbusXXRTU` to `DtgMbXXRTU`.
