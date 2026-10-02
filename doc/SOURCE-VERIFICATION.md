# Source publication verification — 2026-10-02

Source verification used a clean copy exported from each new Git index, without existing build output, Python caches, logs or links. The SDK and STM32 repositories were placed as siblings. Old Gitea commits and remotes are not part of either new repository.

| Check | Result |
|---|---|
| SDK Studio tests on clean source | 100 passed |
| Studio prerequisite check | Passed |
| Skill structural validation | All 9 passed |
| Independent HAL/component/tooling/Studio routing review | Passed after correcting stale Web/source/field references |
| Markdown links and JSON schemas | Valid |
| Both maintained DTS generated-output checks | Current |
| Keil project source FilePath checks | All four projects resolve every referenced source file |
| Git index symbolic links | None in either repository |
| Recognized credential-signature scan | No matches; not a comprehensive security audit |

## Actual firmware rebuild results

Rebuilds used installed Keil ARMCC 5.06 update 5 on the clean source copy; no device was flashed.

| Board project | Result |
|---|---|
| c8t6_xiaoyan_net (SDK App c8t6_ark_net) | Passed, zero errors/warnings; Code 45324, RO-data 2684, RW-data 476, ZI-data 19596 bytes; HEX produced |
| c8t6_microcar_soil | Source compilation completed; link fails because sections exceed the configured Flash region (L6406E/L6407E) |
| c8t6_demo | Legacy JSON-based App has not been migrated to the current DTS-generated API; build fails with missing ark_dts_generated.h and obsolete configuration interfaces |
| c8t6_usb_cdc | Legacy JSON-based App has not been migrated to the current DTS-generated API; build fails with missing ark_dts_generated.h and obsolete configuration interfaces |

The three failing firmware builds are explicitly retained as current limitations, not reported as successful. This source-publication change does not enlarge declared chip Flash or redesign legacy Apps. The maintained network firmware builds successfully; the SDK's automated tooling tests all pass. Actual board behavior and serial/network communication on hardware were not tested.

## Publication preparation

Documentation is consolidated under doc/components, doc/component_template and doc/schemas. The component generator, tests, Skill links and App schema references use the new paths. The independent reviewer found no remaining introduced migration defects. Web implementation is explicitly external and currently absent; ark_web starts as an empty private repository.

Wi-Fi and device passwords in the network DTS are replaced with CHANGE_ME placeholders, and its generated C/H inputs are regenerated through the official generator. Existing runtime logs remain local and are excluded. Fonts, icons, JSON schemas, hand-maintained PyInstaller spec, vendor libraries and licenses remain tracked. The existing SDK LICENSE.txt and third-party licenses are preserved; public hosting does not change their terms.
