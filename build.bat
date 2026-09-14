@echo off
chcp 65001
echo Đang biên dịch Manager...
g++ Manager\*.cpp Shared\*.cpp -o Manager.exe -O2 -luser32 -lgdi32 -lws2_32 -mwindows -DUNICODE -D_UNICODE

echo Đang biên dịch Client...
g++ Client\*.cpp Shared\*.cpp -o Client.exe -O2 -luser32 -lgdi32 -lws2_32 -mwindows -DUNICODE -D_UNICODE

echo Hoàn tất!
