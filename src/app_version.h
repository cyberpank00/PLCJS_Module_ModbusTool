#ifndef APP_VERSION_H
#define APP_VERSION_H

/* Single source of truth for the tool version.
 * Included by C++ (via version.h) AND the Windows resource compiler (app.rc).
 * NO Qt types here — the RC preprocessor does not know about Qt.
 *
 * When bumping a release — change all three defines below. */

#define APP_VERSION_MAJOR  1
#define APP_VERSION_MINOR  0
#define APP_VERSION_PATCH  5
#define APP_VERSION_BUILD  0

/* Comma form for FILEVERSION / PRODUCTVERSION in VERSIONINFO. */
#define APP_VERSION_COMMA  APP_VERSION_MAJOR,APP_VERSION_MINOR,APP_VERSION_PATCH,APP_VERSION_BUILD

/* Plain ASCII string for VALUE "FileVersion" / "ProductVersion".
 * windres does not reliably expand multi-part macro string literals,
 * so we keep this as a single quoted string. Update it together with
 * the three numeric defines above. */
#define APP_VERSION_STR  "1.0.5.0"
#define APP_PRODUCT_VER  "1.00.05"

#endif /* APP_VERSION_H */
