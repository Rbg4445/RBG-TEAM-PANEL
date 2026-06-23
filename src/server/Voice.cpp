#include "Voice.h"

ServerVoice& ServerVoice::GetInstance() {
    static ServerVoice instance;
    return instance;
}
