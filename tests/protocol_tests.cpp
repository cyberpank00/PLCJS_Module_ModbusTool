#include "protocol/BootloaderProtocol.h"
#include "protocol/Pdp.h"
#include <cassert>

int main()
{
    const QString mac = QStringLiteral("02:00:12:34:56:78");
    QVector<quint16> regs(boot::IR_COUNT_WITH_MAC, 0);
    regs[0] = 0xb007; regs[1] = 0x10ad; regs[3] = 0x0108;
    regs[25] = 0x0200; regs[26] = 0x1234; regs[27] = 0x5678;
    auto status = boot::parseStatus(regs);
    assert(status.mac == mac);
    assert(boot::identityError(status, mac).isEmpty());
    assert(!boot::identityError(status, QStringLiteral("02:00:12:34:56:79")).isEmpty());
    assert(boot::normalizeMac(QStringLiteral("02-00-12-34-56-78")) == mac);
    assert(boot::normalizeMac(QStringLiteral("ff:ff:ff:ff:ff:ff")).isEmpty());
    assert(boot::normalizeMac(QStringLiteral("00:00:00:00:00:00")).isEmpty());
    regs.resize(boot::IR_COUNT);
    regs[3] = 0x0107;
    status = boot::parseStatus(regs);
    assert(status.magic == boot::kBootloaderMagic && status.mac.isEmpty());
    assert(!boot::identityError(status, mac).isEmpty());

    pdp::Device a, b, selected;
    a.macStr = mac; a.ip = QStringLiteral("169.254.86.120"); a.inBoot = true;
    b.macStr = QStringLiteral("02:00:12:34:56:79");
    b.ip = QStringLiteral("169.254.86.121"); b.inBoot = true;
    QString error;
    assert(pdp::selectBootloader({a}, {}, selected, &error) && selected.macStr == mac);
    assert(!pdp::selectBootloader({a, b}, {}, selected, &error) && !error.isEmpty());
    assert(pdp::selectBootloader({a, b}, mac, selected, &error) && selected.macStr == mac);
    assert(!pdp::selectBootloader({b}, mac, selected, &error));
    b.ip = a.ip;
    assert(!pdp::selectBootloader({a, b}, mac, selected, &error) && !error.isEmpty());

    QByteArray image(boot::kFwHeaderOffset + 28, '\0');
    image.replace(boot::kFwHeaderOffset, 4, QByteArray::fromHex("4a434c50"));
    assert(boot::parseFwHeader(image).valid);
    image[boot::kFwHeaderOffset + 16] = 1;
    assert(!boot::parseFwHeader(image).valid);
    assert(!boot::parseFwHeader(image.left(boot::kFwHeaderOffset + 16)).valid);
    return 0;
}
