@echo off
chcp 65001
echo Dang bien dich Manager...
g++ -finput-charset=UTF-8 -fexec-charset=UTF-8 Manager\Core\*.cpp Manager\FileTransfer\*.cpp Manager\Media\*.cpp Manager\UI\*.cpp Shared\*.cpp -o Manager.exe -O2 -luser32 -lgdi32 -lws2_32 -lpsapi -lole32 -lgdiplus -lturbojpeg -lwinmm -lcomctl32 -liphlpapi -lshell32 -lcomdlg32 -mwindows -DUNICODE -D_UNICODE

echo Dang bien dich Client...
g++ -finput-charset=UTF-8 -fexec-charset=UTF-8 Client\Core\*.cpp Client\FileTransfer\*.cpp Client\Media\*.cpp Client\System\*.cpp Shared\*.cpp -o Client.exe -O2 -luser32 -lgdi32 -lgdiplus -lole32 -lpsapi -lws2_32 -lturbojpeg -lwinmm -lcomctl32 -liphlpapi -ld3d11 -ldxgi -mwindows -DUNICODE -D_UNICODE

echo Hoan tat!

if "%1"=="test" (
    echo.
    echo Dang bien dich bo kiem thu M3...
    g++ -std=c++17 -finput-charset=UTF-8 -fexec-charset=UTF-8 tests\test_file_transfer.cpp Manager\FileTransfer\FileManger.cpp Client\FileTransfer\FileWorker.cpp Shared\CRC32.cpp Shared\Utils.cpp -o tests\test_file_transfer.exe -O2 -luser32 -lgdi32 -DUNICODE -D_UNICODE
    if errorlevel 1 (
        echo [ERROR] Bien dich test_file_transfer that bai!
        exit /b 1
    )

    echo Dang bien dich bo kiem thu M4...
    g++ -std=c++17 -finput-charset=UTF-8 -fexec-charset=UTF-8 tests\test_remote_features.cpp Client\System\InputInjector.cpp Client\Media\ScreenCapture.cpp Shared\CRC32.cpp Shared\Utils.cpp -o tests\test_remote_features.exe -O2 -luser32 -lgdi32 -ld3d11 -ldxgi -lturbojpeg -DUNICODE -D_UNICODE
    if errorlevel 1 (
        echo [ERROR] Bien dich test_remote_features that bai!
        exit /b 1
    )

    echo.
    echo ========================================
    echo CHAY KIEM THU TEST_FILE_TRANSFER
    echo ========================================
    tests\test_file_transfer.exe
    if errorlevel 1 (
        echo [ERROR] test_file_transfer that bai!
        exit /b 1
    )

    echo.
    echo ========================================
    echo CHAY KIEM THU TEST_REMOTE_FEATURES
    echo ========================================
    tests\test_remote_features.exe
    if errorlevel 1 (
        echo [ERROR] test_remote_features that bai!
        exit /b 1
    )

    echo.
    echo [SUCCESS] Toan bo bo kiem thu M3 ^& M4 da chay va vuot qua 100%!
)
