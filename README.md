# RinJSON

RinJSON provides bounded JSON parsing interfaces for RinOS components, including C++ document parsing and OCI manifest reading.

## Public API contract

| Requirement | Contract |
| --- | --- |
| Purpose | RinJSON provides bounded JSON parsing interfaces for RinOS components, including C++ document parsing and OCI manifest reading. |
| Supported API | Public interfaces are `rinjson/c_reader.h`, `rinjson/json.hpp`, and `rinjson/oci_manifest.hpp`. They parse supplied JSON bytes into caller-visible results; OCI parsing does not fetch referenced content. |
| Unsupported API | The parsers do not resolve URLs, perform network access, validate application authorization, or provide unbounded/general-purpose JSON storage. |
| ownership | The caller owns source bytes. C++ parse results own their parsed storage according to their result type; C interfaces follow the buffer and result ownership stated in their declarations. |
| thread-safety | Separate parser instances and results can be used concurrently. Do not mutate a result while another thread reads it. |
| limits | C++ defaults cap input at 4 MiB, nesting depth at 32, string bytes at 512 KiB, array items at 4096, object members at 1024, nodes at 131,072, and allocation at 16 MiB. |
| errors | Malformed JSON, unsupported shape, or limit exhaustion is reported through the parser result/status. Callers must handle failure and avoid consuming partial results as valid documents. |
| ABI stability | The C header is the C ABI surface; the C++ headers are source interfaces whose ABI follows the selected compiler and build. No cross-version ABI guarantee is published. |
| security | Treat JSON as untrusted input. Keep limits enabled and validate application-specific meaning separately; parsing an OCI reference does not authorize or retrieve it. |
| build | Use the headers under `include/rinjson` from the RinOS build. This repository does not publish a separate build/install workflow. |
| test | No standalone test command is documented here. Validate through the consuming RinOS targets that use these parsers. |
