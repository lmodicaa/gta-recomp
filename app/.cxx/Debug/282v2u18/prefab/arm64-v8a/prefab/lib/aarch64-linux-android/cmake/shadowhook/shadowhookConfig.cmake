if(NOT TARGET shadowhook::shadowhook)
add_library(shadowhook::shadowhook SHARED IMPORTED)
set_target_properties(shadowhook::shadowhook PROPERTIES
    IMPORTED_LOCATION "C:/Users/Administrator/.gradle/caches/transforms-4/957cb01a6c79364df8f23114c0dbf410/transformed/jetified-shadowhook-1.0.10/prefab/modules/shadowhook/libs/android.arm64-v8a/libshadowhook.so"
    INTERFACE_INCLUDE_DIRECTORIES "C:/Users/Administrator/.gradle/caches/transforms-4/957cb01a6c79364df8f23114c0dbf410/transformed/jetified-shadowhook-1.0.10/prefab/modules/shadowhook/include"
    INTERFACE_LINK_LIBRARIES ""
)
endif()

