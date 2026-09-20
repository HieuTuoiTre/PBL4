#pragma once

namespace Protocol
{
    enum MessageType{
        MSG_PING = 1,
        MSG_PONG,
        MSG_DISCONNECT,

        MSG_VIDEO_FRAME,
        MSG_AUDIO_CHUNK,

        MSG_MOUSE_EVENT,
        MSG_KEY_EVENT,

        MSG_SYS_INFO,
        MSG_PROCESS_LIST,
        MSG_KILL_PROCESS,
    };
} 
