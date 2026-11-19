Minimal freestanding C++ support headers for the Windows LLVM build of Charon
(tools/build-charon-windows.py). The Haiku GCC build gets the same three
declarations from libstdc++; Charon uses only placement new, std::initializer_list
and std::is_trivially_destructible, and links no C++ runtime.
