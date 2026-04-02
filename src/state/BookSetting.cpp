#include "state/BookSetting.h"
#include "system/FontManager.h"

int BookSettings::getReaderFontId() const {
    return FontManager::getFontId(fontFamily, fontSize);
}