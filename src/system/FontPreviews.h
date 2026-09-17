#pragma once

#include <string>

class GfxRenderer;

namespace FontPreviews {

/** Register the compact built-in package-name preview faces with the renderer. */
void initialize(GfxRenderer& renderer);

/** Return the preview face ID for a FontPackageManager install-family name. */
int fontIdForFamily(const std::string& family);

}  // namespace FontPreviews
