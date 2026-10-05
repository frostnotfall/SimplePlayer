#pragma once
#include "SubtitleProvider.h"
#include <QString>

namespace bp {
inline const GUID SubtitleMedia={0xe487eb08,0x6b26,0x4be9,{0x9d,0xd3,0x99,0x34,0x34,0xd3,0x13,0xfd}};
bool isTextSubtitleType(const AM_MEDIA_TYPE& type);
class TextSubtitleSink;
Microsoft::WRL::ComPtr<IBaseFilter> makeTextSubtitleSink(SubtitleProvider* provider,TextSubtitleSink** sink);
void selectEmbeddedSlot(TextSubtitleSink* sink,int slot);
int selectedEmbeddedSlot(TextSubtitleSink* sink);
QString embeddedLabel(TextSubtitleSink* sink);
}
