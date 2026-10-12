@echo off
chcp 65001
echo Đang bien dich Manager...
g++ Manager\Core\*.cpp Manager\FileTransfer\*.cpp Manager\Media\*.cpp Manager\UI\*.cpp Shared\*.cpp -o Manager.exe -O2 -luser32 -lgdi32 -lws2_32 -lpsapi -lole32 -lgdiplus -mwindows -DUNICODE -D_UNICODE

echo Đang bien dich Client...
g++ Client\Core\*.cpp Client\FileTransfer\*.cpp Client\Media\*.cpp Client\System\*.cpp Shared\*.cpp -o Client.exe -O2 -luser32 -lgdi32 -lgdiplus -lole32 -lpsapi -lws2_32 -mwindows -DUNICODE -D_UNICODE

echo Hoan tat!