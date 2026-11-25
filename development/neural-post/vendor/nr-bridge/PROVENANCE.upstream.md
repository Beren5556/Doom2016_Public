# nr-bridge component

Imported nr_direct.h and nr_forwarder.cpp from LowPopLabs/nr-bridge commit
0bb6bf86e13ac19a85d7693062c1f74cfcd5f77f (26 October 2026 retrieval).
https://github.com/lowpoplabs/nr-bridge
Own code is MIT: LICENSE. Our adapter is in src/NeuralPass.*.

The header ngx/nvsdk_ngx_defs.h is NVIDIA SDK material, under
NVIDIA-SDK-LICENSE.txt, fetched by CMake from pinned NVIDIA/DLSS commit
9981c6350dc58c0ce21f024fd65e31e851c31689 and verified by SHA256.
This software contains source code provided by NVIDIA Corporation.
The proprietary Neural model is not part of this tree.
For the local experiment a model already installed by the user is imported into
a private NeuralRuntime directory; it must not be added to source archives.

The component approximates camera rotation vectors and supplies constant depth.
It does not recover engine object motion, depth, or positional parallax.
The two eyes occupy separate rectangles of one model input/history. Stereo
consistency of neural appearance is not guaranteed by the memory-isolation tests.

