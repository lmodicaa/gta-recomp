@echo off
"C:\\Program Files\\Java\\jdk-17\\bin\\java" ^
  --class-path ^
  "C:\\Users\\Administrator\\.gradle\\caches\\modules-2\\files-2.1\\com.google.prefab\\cli\\2.1.0\\aa32fec809c44fa531f01dcfb739b5b3304d3050\\cli-2.1.0-all.jar" ^
  com.google.prefab.cli.AppKt ^
  --build-system ^
  cmake ^
  --platform ^
  android ^
  --abi ^
  armeabi-v7a ^
  --os-version ^
  26 ^
  --stl ^
  c++_static ^
  --ndk-version ^
  26 ^
  --output ^
  "C:\\Users\\ADMINI~1\\AppData\\Local\\Temp\\2\\agp-prefab-staging11829604455328620365\\staged-cli-output" ^
  "C:\\Users\\Administrator\\.gradle\\caches\\transforms-4\\957cb01a6c79364df8f23114c0dbf410\\transformed\\jetified-shadowhook-1.0.10\\prefab"
