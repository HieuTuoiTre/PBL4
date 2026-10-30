@echo off
chcp 65001
echo Dang bien dich Manager...
g++ -finput-charset=UTF-8 -fexec-charset=UTF-8 Manager\Core\*.cpp Manager\FileTransfer\*.cpp Manager\Media\*.cpp Manager\UI\*.cpp Shared\*.cpp -o Manager.exe -O2 -luser32 -lgdi32 -lws2_32 -lpsapi -lole32 -lgdiplus -lturbojpeg -lwinmm -lcomctl32 -liphlpapi -ld2d1 -mwindows -DUNICODE -D_UNICODE

echo Dang bien dich Client...
g++ -finput-charset=UTF-8 -fexec-charset=UTF-8 Client\Core\*.cpp Client\FileTransfer\*.cpp Client\Media\*.cpp Client\System\*.cpp Shared\*.cpp -o Client.exe -O2 -luser32 -lgdi32 -lgdiplus -lole32 -lpsapi -lws2_32 -lturbojpeg -lwinmm -lcomctl32 -liphlpapi -ld3d11 -ldxgi -mwindows -DUNICODE -D_UNICODE

echo Hoan tat!
