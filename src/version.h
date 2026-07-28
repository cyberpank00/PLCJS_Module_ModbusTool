#ifndef VERSION_H
#define VERSION_H

/* Pull in the single-source version numbers shared with the RC file. */
#include "app_version.h"

/* Qt helper string — "v1.00.05" — used in the window title.
 * (QStringLiteral / arg() are Qt-only; that is why this lives here and
 *  not in app_version.h which must stay RC-compiler-compatible.) */
#define MODULE_TOOL_VERSION_QSTR \
    QStringLiteral("v%1.%2.%3") \
        .arg(APP_VERSION_MAJOR) \
        .arg(APP_VERSION_MINOR, 2, 10, QChar('0')) \
        .arg(APP_VERSION_PATCH, 2, 10, QChar('0'))

#endif // VERSION_H
