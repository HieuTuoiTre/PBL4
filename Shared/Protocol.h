#pragma once

#include <cstdint>

//uint8_t = 1 byte

namespace Protocol
{
    enum MessageType : uint8_t {
        //connection
        MSG_PING = 1,        //request
        MSG_PONG,            //reponse
        MSG_DISCONNECT,

        //video/audio
        MSG_VIDEO_FRAME,
        MSG_AUDIO_CHUNK,

        //mouse/keyboard control
        MSG_MOUSE_EVENT,
        MSG_KEY_EVENT,

        //task manager
        MSG_SYS_INFO_REQUEST,
        MSG_SYS_INFO_RESPONSE,
        MSG_PROCESS_LIST_REQUEST,
        MSG_PROCESS_LIST_RESPONSE,
        MSG_KILL_PROCESS,

        //file transfer
        MSG_DRIVE_LIST_REQUEST,
        MSG_DRIVE_LIST_RESPONSE,
        MSG_DIR_REQUEST,      //request to see path content  
        MSG_DIR_RESPONSE,     //response of path conten
        MSG_FILE_INFO,        //name, size of file
        MSG_FILE_CHUNK,
        MSG_FILE_END
    };

    enum MouseEventType : uint8_t {
        MOUSE_ACTION_MOVE = 1,
        MOUSE_ACTION_PRESS, 
        MOUSE_ACTION_RELEASE,
        MOUSE_ACTION_WHEEL
    };

    enum MouseButtonType : uint8_t {
        BUTTON_NONE = 0,
        BUTTON_LEFT,
        BUTTON_RIGHT,
        BUTTON_MIDDLE
    };

    #pragma pack(push, 1)

    struct PacketHeader{ //total size = type (1 byte) + content (4 byte) = 5 byte
        MessageType type;
        uint32_t size; // 4 byte, 2 byte (uint16_t) would be too small; while 16 byte (uint64_t) wouldve been too large and unneeded
    };

    struct MousePayload{
        uint8_t actionType;
        uint8_t buttonType;
        int16_t x;
        int16_t y;
        int16_t wheelDelta;
    };

    struct KeyboardPayload{
        uint32_t vkCode;
        uint8_t isDown; //down = pressed, vice versa
    };

    struct SysInfoPayload{
        uint8_t cpuUsage;
        uint8_t ramUsage;
        uint8_t diskUsage;
    };

    struct ProcessInfo{
        uint32_t pid;
        uint32_t ramUsageMB;
        uint32_t diskUsageMB;
        char name[256];
    };

    struct FileManager{
        uint64_t fileSize;
        char fileName[256];
    };
    
    #pragma pack(pop)
}   
