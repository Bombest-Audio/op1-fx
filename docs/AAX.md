# AAX

CMake builds AAX when `-DOP1FX_AAX_SDK=<path to the AAX SDK>` is set. The result is an unsigned `.aaxplugin`.

An unsigned AAX only loads in a development build of Pro Tools. Shipping one needs PACE signing with `wraptool`, the iLok
USB key and a wrap configuration (WCGUID) for each plugin created in PACE Central. Use the Bombest AAX signing rule
and the wraptool sign command from it. Each plugin here needs its own WCGUID before it can be signed.
